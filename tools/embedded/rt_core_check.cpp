/// AmbiTap: target-independent ambisonics library
/// Embedded real-time profile compile gate.
///
/// This translation unit is cross-compiled for bare-metal Cortex-M55 in CI
/// (arm-none-eabi-g++ -mcpu=cortex-m55 -fno-exceptions -fno-rtti; see
/// .github/workflows/ci.yml) to prove that the RT-profile subset of the
/// library stays free of exceptions, RTTI, std::thread, Eigen, and hardware
/// doubles in its per-sample paths. It instantiates every processor in the
/// profile and runs one block through each.
///
/// The profile (allocation happens at construction/prepare time only; every
/// process path is allocation-free and wait-free — enforced on the host by
/// tests/test_rt_safety.cpp):
///   encoder, mirror, virtual_mic, directional_loudness, spatial_compressor,
///   doppler, format_converter, matrix_applier (drive it with precomputed
///   rotation/decode matrices), binaural_core (float32 convolution), and
///   analysis::energy_vector.
///
/// NOT in the profile (host/control side): decoder & rotator matrix
/// construction (Eigen), async_rebuilder (std::thread), binaural_renderer's
/// resampling/orientation layer, soundfield_grid, the SOFA reader.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include <ambitap/analysis/energy_vector.h>
#include <ambitap/dsp/binaural_core.h>
#include <ambitap/dsp/directional_loudness.h>
#include <ambitap/dsp/doppler.h>
#include <ambitap/dsp/encoder.h>
#include <ambitap/dsp/format_converter.h>
#include <ambitap/dsp/mirror.h>
#include <ambitap/dsp/spatial_compressor.h>
#include <ambitap/dsp/util/matrix_applier.h>
#include <ambitap/dsp/virtual_mic.h>
#include <ambitap/math/binaural/hrtf_data.h>
#include <ambitap/math/core/fast_math.h>

#include <cstddef>

namespace {

    constexpr int    k_order = 3;
    constexpr size_t k_chans = ambitap::channel_count(k_order);
    constexpr size_t k_block = 64;

    float g_in[k_chans][k_block];
    float g_out[k_chans][k_block];

    void planar(const float* in[k_chans], float* out[k_chans]) {
        for (size_t c = 0; c < k_chans; ++c) {
            in[c]  = g_in[c];
            out[c] = g_out[c];
        }
    }

} // namespace

int main() {
    using namespace ambitap;

    const float* in[k_chans];
    float*       out[k_chans];
    planar(in, out);
    for (size_t c = 0; c < k_chans; ++c) {
        for (size_t i = 0; i < k_block; ++i) g_in[c][i] = 0.25f;
    }

    float acc = 0.f;

    {
        dsp::encoder enc(k_order);
        enc.set_direction(0.5f, 0.1f);
        enc.snap_parameters();
        float frame[k_chans];
        enc.process_sample(1.f, frame);
        acc += frame[0];
    }
    {
        dsp::mirror mir(k_order);
        mir.set_flip_lr(true);
        mir.snap_parameters();
        mir.process(in, out, k_block);
        acc += g_out[1][0];
    }
    {
        dsp::virtual_mic mic(k_order);
        mic.set_direction(0.f, 0.f);
        mic.snap_parameters();
        float mono[k_block];
        mic.process(in, mono, k_block);
        acc += mono[0];
    }
    {
        dsp::directional_loudness dl(k_order);
        dl.set_direction(0.f, 0.f);
        dl.snap_parameters();
        dl.process(in, out, k_block);
        acc += g_out[0][0];
    }
    {
        dsp::spatial_compressor comp(k_order);
        comp.prepare(48000.f);
        comp.set_threshold_db(-20.f);
        comp.process(in, out, k_block);
        acc += g_out[0][0];
    }
    {
        dsp::doppler dop(k_order);
        dop.set_distance(2.f);
        dop.set_max_distance(10.f);
        dop.prepare(48000.f);
        dop.process(in, out, k_block);
        acc += g_out[0][0];
    }
    {
        dsp::format_converter conv(1);
        conv.set_direction(dsp::format_direction::fuma_to_ambix);
        const float* fin[4]  = {g_in[0], g_in[1], g_in[2], g_in[3]};
        float*       fout[4] = {g_out[0], g_out[1], g_out[2], g_out[3]};
        conv.process(fin, fout, k_block);
        acc += g_out[0][0];
    }
    {
        // Precomputed-matrix path: identity in, identity prev.
        static float mat[k_chans * k_chans];
        for (size_t d = 0; d < k_chans; ++d) mat[d * k_chans + d] = 1.f;
        dsp::matrix_applier applier;
        applier.apply(mat, mat, mat, k_chans, k_chans, in, out, k_block, false);
        acc += g_out[0][0];
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
        bin.prepare(k_block, left, right, builtin_hrtf_length);
        float l[k_block];
        float r[k_block];
        bin.process(in, l, r, k_block);
        acc += l[0] + r[0];
    }
    {
        analysis::energy_vector ev;
        ev.prepare(48000.f);
        float  vx[k_block];
        float  vy[k_block];
        float  vz[k_block];
        float* v[3] = {vx, vy, vz};
        ev.process(in, v, k_block);
        acc += vx[k_block - 1];
    }

    acc += fast_linear_from_db(fast_db_from_linear(0.5f));

    // Keep everything observable so nothing is optimized away.
    return acc > -1e9f ? 0 : 1;
}
