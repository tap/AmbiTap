/// @file test_dsp_binaural.cpp
/// @brief Tests for dsp::binaural_renderer.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/binaural_renderer.h"

using namespace tap::ambi;

namespace {

    // Planar input helper: channels x frames, all zero.
    struct planar {
        std::vector<std::vector<float>> bufs;
        std::vector<const float*>       ptrs;
        planar(size_t channels, size_t frames)
            : bufs(channels, std::vector<float>(frames, 0.f)) {
            for (auto& b : bufs) {
                ptrs.push_back(b.data());
            }
        }
    };

} // namespace

TEST(DspBinaural, SilentUntilPrepared) {
    dsp::binaural_renderer bin(1);
    EXPECT_FALSE(bin.is_prepared());

    planar             in(4, 64);
    std::vector<float> left(64, 1.f), right(64, 1.f);
    bin.process(in.ptrs.data(), left.data(), right.data(), 64);
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(left[i], 0.f);
        EXPECT_EQ(right[i], 0.f);
    }
}

TEST(DspBinaural, WImpulseReproducesOmniHrtf) {
    constexpr size_t       block = 64;
    dsp::binaural_renderer bin(1);
    bin.prepare(block);
    ASSERT_TRUE(bin.is_prepared());

    // Impulse on W only: the output must equal the ACN-0 HRTF FIR per ear.
    std::vector<float> left_all, right_all;
    planar             in(4, block);
    std::vector<float> left(block), right(block);
    const size_t       blocks_needed = builtin_hrtf_length / block;
    for (size_t b = 0; b < blocks_needed; ++b) {
        std::fill(in.bufs[0].begin(), in.bufs[0].end(), 0.f);
        if (b == 0) {
            in.bufs[0][0] = 1.f;
        }
        bin.process(in.ptrs.data(), left.data(), right.data(), block);
        left_all.insert(left_all.end(), left.begin(), left.end());
        right_all.insert(right_all.end(), right.begin(), right.end());
    }

    for (size_t t = 0; t < builtin_hrtf_length; ++t) {
        EXPECT_NEAR(left_all[t], builtin_hrtf_left[0][t], 1e-4f) << "t=" << t;
        EXPECT_NEAR(right_all[t], builtin_hrtf_right[0][t], 1e-4f) << "t=" << t;
    }
}

TEST(DspBinaural, VolumeScalesOutput) {
    constexpr size_t       block = 64;
    dsp::binaural_renderer a(1), b(1);
    a.prepare(block);
    b.prepare(block);
    b.set_volume(0.5f);

    // Volume ramps to its target across one block; run a silent block through
    // both renderers so the comparison below sees the settled gain.
    {
        planar             silence(4, block);
        std::vector<float> l(block), r(block);
        a.process(silence.ptrs.data(), l.data(), r.data(), block);
        b.process(silence.ptrs.data(), l.data(), r.data(), block);
    }

    planar in(4, block);
    in.bufs[0][0] = 1.f;
    in.bufs[3][0] = 0.7f;
    std::vector<float> la(block), ra(block), lb(block), rb(block);
    a.process(in.ptrs.data(), la.data(), ra.data(), block);
    b.process(in.ptrs.data(), lb.data(), rb.data(), block);

    for (size_t i = 0; i < block; ++i) {
        EXPECT_NEAR(lb[i], 0.5f * la[i], 1e-6f);
        EXPECT_NEAR(rb[i], 0.5f * ra[i], 1e-6f);
    }
}

TEST(DspBinaural, MaglsProjectionDiffersFromLs) {
    constexpr size_t       block = 64;
    dsp::binaural_renderer ls(2), magls(2);
    ls.prepare(block);
    magls.set_projection(dsp::binaural_renderer::hrtf_projection::magls);
    magls.prepare(block);

    planar in(9, block);
    in.bufs[0][0] = 1.f;
    std::vector<float> l1(block), r1(block), l2(block), r2(block);
    ls.process(in.ptrs.data(), l1.data(), r1.data(), block);
    magls.process(in.ptrs.data(), l2.data(), r2.data(), block);

    float diff = 0.f;
    for (size_t i = 0; i < block; ++i) {
        diff += std::fabs(l1[i] - l2[i]);
    }
    EXPECT_GT(diff, 1e-4f);
}

TEST(DspBinaural, HeadTrackingChangesOutput) {
    constexpr size_t       block = 64;
    dsp::binaural_renderer still(3), turned(3);
    still.prepare(block);
    turned.prepare(block);
    turned.set_head_orientation(k_pi * 0.5f, 0.f, 0.f);
    turned.wait_for_settling();

    // A lateral source (left) so rotation has an audible effect.
    planar in(16, block);
    float  sh[k_max_channel_count];
    evaluate_sh(3, k_pi * 0.5f, 0.f, sh);
    for (size_t ch = 0; ch < 16; ++ch) {
        in.bufs[ch][0] = sh[ch];
    }

    std::vector<float> l1(block), r1(block), l2(block), r2(block);
    still.process(in.ptrs.data(), l1.data(), r1.data(), block);
    turned.process(in.ptrs.data(), l2.data(), r2.data(), block);

    float diff = 0.f;
    for (size_t i = 0; i < block; ++i) {
        diff += std::fabs(l1[i] - l2[i]);
    }
    EXPECT_GT(diff, 1e-4f);
}

// Audit finding C7: head tracking must counter-rotate the scene. With the
// head turned 90° to the LEFT, a front source is physically at the listener's
// RIGHT — the right-ear energy must dominate. (The old code rotated the scene
// *by* the head orientation, putting the source at the left ear instead.)
TEST(DspBinaural, HeadTrackingCounterRotatesTheScene) {
    constexpr size_t block = 64;
    constexpr int    order = 3;

    dsp::binaural_renderer bin(order);
    bin.prepare(block);
    bin.set_head_orientation(k_pi * 0.5f, 0.f, 0.f); // look left
    bin.wait_for_settling();

    // Run out the rotation-adoption crossfade with silence before exciting.
    {
        planar             silence(16, block);
        std::vector<float> l(block), r(block);
        for (size_t i = 0; i < dsp::rotator::k_fade_samples / block + 1; ++i) {
            bin.process(silence.ptrs.data(), l.data(), r.data(), block);
        }
    }

    // Front source, impulse excitation.
    planar in(16, block);
    float  sh[k_max_channel_count];
    evaluate_sh(order, 0.f, 0.f, sh);
    for (size_t ch = 0; ch < 16; ++ch) {
        in.bufs[ch][0] = sh[ch];
    }

    float              e_left = 0.f, e_right = 0.f;
    std::vector<float> l(block), r(block);
    // Run several blocks so the full HRIR (128 taps > one block) is captured.
    for (int b = 0; b < 4; ++b) {
        bin.process(in.ptrs.data(), l.data(), r.data(), block);
        for (size_t i = 0; i < block; ++i) {
            e_left += l[i] * l[i];
            e_right += r[i] * r[i];
        }
        for (size_t ch = 0; ch < 16; ++ch) {
            in.bufs[ch][0] = 0.f; // impulse only once
        }
    }

    EXPECT_GT(e_right, 2.0f * e_left) << "head turned left => front source at the right ear (E_L=" << e_left
                                      << ", E_R=" << e_right << ")";
}

TEST(DspBinaural, ProbeResponseShapeAndNormalization) {
    dsp::binaural_renderer bin(1);
    const auto             r = bin.probe_response(0.5f, 0.1f);

    EXPECT_EQ(r.frequencies.size(), 257u); // 512-point FFT -> 257 bins
    EXPECT_EQ(r.left_db.size(), 257u);
    EXPECT_EQ(r.right_db.size(), 257u);
    EXPECT_FLOAT_EQ(r.frequencies[0], 0.f);

    // Common normalization: the louder ear peaks at exactly 0 dB.
    float peak = -1e9f;
    for (float v : r.left_db) {
        peak = std::max(peak, v);
    }
    for (float v : r.right_db) {
        peak = std::max(peak, v);
    }
    EXPECT_NEAR(peak, 0.f, 1e-3f);
}

// Audit finding B7: the built-in HRTFs are 44.1 kHz data; prepare() must
// adapt them to the host rate or every spectral cue shifts and ITDs shrink.
TEST(DspBinaural, PrepareResamplesBuiltinHrtfToHostRate) {
    constexpr size_t block = 64;

    // Reference: W-only impulse at the native rate reproduces the raw FIR.
    dsp::binaural_renderer native(1);
    native.prepare(block); // 44.1 kHz default

    // Same impulse at 2x the rate: the response must stretch to ~2x the
    // length — its energy centroid lands at ~2x the native centroid.
    dsp::binaural_renderer doubled(1);
    doubled.prepare(block, 2.f * builtin_hrtf_sample_rate);

    auto centroid = [](dsp::binaural_renderer& bin) {
        planar in(4, block);
        in.bufs[0][0] = 1.f;
        std::vector<float> l(block), r(block);
        double             num = 0.0, den = 0.0;
        size_t             t = 0;
        for (size_t b = 0; b < 6; ++b) {
            bin.process(in.ptrs.data(), l.data(), r.data(), block);
            for (size_t i = 0; i < block; ++i, ++t) {
                num += static_cast<double>(t) * l[i] * l[i];
                den += l[i] * l[i];
            }
            in.bufs[0][0] = 0.f;
        }
        return num / den;
    };

    const double c_native  = centroid(native);
    const double c_doubled = centroid(doubled);
    EXPECT_NEAR(c_doubled / c_native, 2.0, 0.1) << "native centroid " << c_native << ", 2x-rate centroid " << c_doubled;

    // Sanity: preparing at the native rate must reproduce the FIR bit-close
    // (the resampling path must not engage).
    EXPECT_FLOAT_EQ(native.sample_rate(), builtin_hrtf_sample_rate);
}

// resample_fir basics: identity at equal rates; a delta stretches by the
// rate ratio with unity peak amplitude (within windowed-sinc ripple).
TEST(ResampleFir, DeltaAndIdentity) {
    std::vector<float> delta(64, 0.f);
    delta[20] = 1.f;

    const auto same = resample_fir(delta.data(), delta.size(), 48000.f, 48000.f);
    ASSERT_EQ(same.size(), delta.size());
    EXPECT_FLOAT_EQ(same[20], 1.f);

    const auto up = resample_fir(delta.data(), delta.size(), 44100.f, 88200.f);
    ASSERT_EQ(up.size(), 128u);
    size_t peak = 0;
    for (size_t i = 1; i < up.size(); ++i) {
        if (std::fabs(up[i]) > std::fabs(up[peak])) {
            peak = i;
        }
    }
    EXPECT_EQ(peak, 40u);
    EXPECT_NEAR(up[peak], 1.f, 0.02f);
}
