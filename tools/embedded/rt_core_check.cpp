/// @file rt_core_check.cpp
/// @brief Embedded real-time profile gate.
///
///        Cross-compiled for bare-metal Cortex-M55 in CI (arm-none-eabi-g++
///        -mcpu=cortex-m55 -fno-exceptions -fno-rtti; see .github/workflows/ci.yml)
///        to prove the RT-profile subset stays free of exceptions, RTTI,
///        std::thread, Eigen, and hardware doubles — and then EXECUTED on the
///        target ISA under QEMU (mps3-an547, semihosting; startup + linker script
///        in tools/embedded/qemu/). Each processor runs real blocks and self-checks
///        its output; the program prints RT-PROFILE-PASS and exits 0 on success.
///        It also builds and runs as a plain host program.
///
///        The profile (allocation happens at construction/prepare time only; every
///        process path is allocation-free and wait-free — enforced on the host by
///        tests/test_rt_safety.cpp):
///          encoder, mirror, virtual_mic, directional_loudness, spatial_compressor,
///          doppler, format_converter, matrix_applier + sh_block_applier (rotation
///          matrices built on-device via compute_sh_rotation, decode matrices
///          precomputed), binaural_core (float32 shared-spectrum convolver bank),
///          and analysis::energy_vector.
///
///        NOT in the profile (host/control side): decode-matrix construction
///        (Eigen), async_rebuilder (std::thread), binaural_renderer's
///        resampling/orientation layer, soundfield_grid, the SOFA reader.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <cmath>
#include <cstddef>
#include <cstdio>

#include <ambitap/analysis/energy_vector.h>
#include <ambitap/dsp/binaural_core.h>
#include <ambitap/dsp/directional_loudness.h>
#include <ambitap/dsp/doppler.h>
#include <ambitap/dsp/encoder.h>
#include <ambitap/dsp/format_converter.h>
#include <ambitap/dsp/mirror.h>
#include <ambitap/dsp/spatial_compressor.h>
#include <ambitap/dsp/util/matrix_applier.h>
#include <ambitap/dsp/util/sh_block_applier.h>
#include <ambitap/dsp/virtual_mic.h>
#include <ambitap/math/binaural/hrtf_data.h>
#include <ambitap/math/core/fast_math.h>
#include <ambitap/math/core/rotation_recurrence.h>

namespace {

    constexpr int    k_order = 3;
    constexpr size_t k_chans = tap::ambi::channel_count(k_order);
    constexpr size_t k_block = 64;

    float g_in[k_chans][k_block];
    float g_out[k_chans][k_block];
    float g_out2[k_chans][k_block];

    int g_failures = 0;

    void check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool near(float a, float b, float tol) {
        const float d = a - b;
        return (d < 0 ? -d : d) <= tol;
    }

    bool finite_block(const float* x, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (!(x[i] > -1e9f && x[i] < 1e9f))
                return false;
        }
        return true;
    }

} // namespace

int main() {
    using namespace tap::ambi;

    const float* in[k_chans];
    float*       out[k_chans];
    float*       out2[k_chans];
    for (size_t c = 0; c < k_chans; ++c) {
        in[c]   = g_in[c];
        out[c]  = g_out[c];
        out2[c] = g_out2[c];
        for (size_t i = 0; i < k_block; ++i)
            g_in[c][i] = 0.25f;
    }

    {
        dsp::encoder enc(k_order);
        enc.set_direction(0.5f, 0.1f);
        enc.snap_parameters();
        float frame[k_chans];
        enc.process_sample(1.f, frame);
        check(near(frame[0], 1.f, 1e-6f), "encoder W == input (SN3D)");
    }
    {
        dsp::mirror mir(k_order);
        mir.set_flip_lr(true);
        mir.snap_parameters();
        mir.process(in, out, k_block);
        check(near(g_out[1][0], -0.25f, 1e-6f), "mirror flip_lr negates Y");
        check(near(g_out[0][0], 0.25f, 1e-6f), "mirror flip_lr keeps W");
    }
    {
        dsp::virtual_mic mic(k_order);
        mic.set_direction(0.f, 0.f);
        mic.snap_parameters();
        float mono[k_block];
        mic.process(in, mono, k_block);
        check(finite_block(mono, k_block), "virtual_mic output finite");
    }
    {
        dsp::directional_loudness dl(k_order);
        dl.set_direction(0.f, 0.f);
        dl.snap_parameters();
        dl.process(in, out, k_block);
        check(finite_block(g_out[0], k_block), "directional_loudness output finite");
    }
    {
        dsp::spatial_compressor comp(k_order);
        comp.prepare(48000.f);
        comp.set_threshold_db(-20.f);
        comp.process(in, out, k_block);
        // Sample 0: the envelope has barely moved, so the gain is exactly 1.
        check(near(g_out[0][0], 0.25f, 1e-6f), "compressor unity gain below threshold");
        check(finite_block(g_out[0], k_block), "compressor output finite");
    }
    {
        dsp::doppler dop(k_order);
        dop.set_distance(2.f); // ~280 samples of delay at 48 kHz
        dop.set_max_distance(10.f);
        dop.prepare(48000.f);
        dop.process(in, out, k_block);
        check(near(g_out[0][0], 0.f, 1e-9f), "doppler silent inside time-of-flight");
        for (int b = 0; b < 6; ++b)
            dop.process(in, out, k_block); // past the onset
        check(near(g_out[0][k_block - 1], 0.25f, 1e-3f), "doppler passes signal after delay");
    }
    {
        dsp::format_converter conv(1);
        conv.set_direction(dsp::format_direction::fuma_to_ambix);
        const float* fin[4]  = {g_in[0], g_in[1], g_in[2], g_in[3]};
        float*       fout[4] = {g_out[0], g_out[1], g_out[2], g_out[3]};
        conv.process(fin, fout, k_block);
        // FuMa W is -3 dB relative to AmbiX: 0.25 * sqrt(2) ~ 0.3536.
        check(near(g_out[0][0], 0.35355339f, 1e-4f), "format_converter FuMa W scaling");
    }
    {
        // On-device head-tracking: rotation matrices from the recurrence,
        // applied block-diagonally and densely — outputs must agree.
        static float mat[k_chans * k_chans];
        static float prev[k_chans * k_chans];
        compute_sh_rotation(k_order, 0.4f, -0.1f, 0.05f, mat);
        compute_sh_rotation(k_order, 0.3f, -0.1f, 0.05f, prev);

        dsp::sh_block_applier rot_applier;
        rot_applier.apply(mat, mat, prev, k_order, in, out, k_block, false);
        dsp::matrix_applier dense_applier;
        dense_applier.apply(mat, mat, prev, k_chans, k_chans, in, out2, k_block, false);

        bool equal = true;
        for (size_t c = 0; c < k_chans && equal; ++c) {
            for (size_t i = 0; i < k_block; ++i) {
                if (!near(g_out[c][i], g_out2[c][i], 1e-6f)) {
                    equal = false;
                    break;
                }
            }
        }
        check(equal, "sh_block_applier == matrix_applier on rotation");
        check(near(g_out[0][k_block - 1], 0.25f, 1e-6f), "rotation leaves W invariant");
    }
    {
        // Binaural: built-in FIR tables straight from flash (44.1 kHz data;
        // a real deployment resamples offline for its host rate).
        const float* left[k_chans];
        const float* right[k_chans];
        for (size_t c = 0; c < k_chans; ++c) {
            left[c]  = builtin_hrtf_left[c];
            right[c] = builtin_hrtf_right[c];
        }
        dsp::binaural_core bin(k_order);
        check(bin.prepare(k_block, left, right, builtin_hrtf_length), "binaural_core prepare");
        float l[k_block];
        float r[k_block];
        bin.process(in, l, r, k_block);
        bin.process(in, l, r, k_block);
        check(finite_block(l, k_block) && finite_block(r, k_block), "binaural output finite");
        float peak = 0.f;
        for (size_t i = 0; i < k_block; ++i)
            peak = peak > l[i] ? peak : l[i];
        check(peak > 1e-4f, "binaural output nonzero");
    }
    {
        analysis::energy_vector ev;
        ev.prepare(48000.f);
        float  vx[k_block];
        float  vy[k_block];
        float  vz[k_block];
        float* v[3] = {vx, vy, vz};
        ev.process(in, v, k_block);
        check(vx[k_block - 1] > 0.f, "energy vector points at the source");
    }
    {
        const float g = fast_linear_from_db(fast_db_from_linear(0.5f));
        check(near(g, 0.5f, 1e-4f), "fast_math dB round trip");
    }

    if (g_failures == 0) {
        std::printf("RT-PROFILE-PASS (%d channels, %d-sample blocks)\n", static_cast<int>(k_chans),
                    static_cast<int>(k_block));
        return 0;
    }
    std::printf("RT-PROFILE-FAIL (%d failures)\n", g_failures);
    return 1;
}
