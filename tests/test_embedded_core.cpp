/// AmbiTap: target-independent ambisonics library
/// Tests for the embedded real-time profile: fast math, the float32
/// convolver, the freestanding matrix applier, and the binaural core.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/dsp/binaural_core.h"
#include "ambitap/dsp/spatial_compressor.h"
#include "ambitap/dsp/util/matrix_applier.h"
#include "ambitap/math/binaural/convolution.h"
#include "ambitap/math/binaural/hrtf_data.h"
#include "ambitap/math/core/fast_math.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace ambitap;

// ---------------------------------------------------------------------------
// fast_math: the approximations must be far inside the compressor's audible
// tolerance (the gates here are ~10x looser than the measured errors).
// ---------------------------------------------------------------------------

TEST(FastMath, Log2AccuracyOverAudioRange) {
    // Sweep 1e-12 .. 1e4 (240 dB of range) log-spaced.
    for (int i = 0; i <= 4000; ++i) {
        const float x     = std::pow(10.f, -12.f + 16.f * static_cast<float>(i) / 4000.f);
        const float exact = std::log2(x);
        EXPECT_NEAR(fast_log2(x), exact, 2e-5f) << "x = " << x;
    }
}

TEST(FastMath, Exp2AccuracyOverAudioRange) {
    for (int i = 0; i <= 4000; ++i) {
        const float x     = -60.f + 90.f * static_cast<float>(i) / 4000.f;
        const float exact = std::exp2(x);
        const float rel   = std::abs(fast_exp2(x) - exact) / exact;
        EXPECT_LT(rel, 2e-6f) << "x = " << x;
    }
}

TEST(FastMath, DbRoundTrip) {
    for (float db = -120.f; db <= 24.f; db += 0.25f) {
        const float lin = fast_linear_from_db(db);
        EXPECT_NEAR(lin, std::pow(10.f, db / 20.f), lin * 1e-5f) << db;
        EXPECT_NEAR(fast_db_from_linear(lin), db, 2e-4f) << db;
    }
}

TEST(FastMath, CompressorGainMatchesLibmFormula) {
    // The full gain computer against the exact-libm formula it replaced.
    dsp::spatial_compressor comp(1);
    comp.prepare(48000.f);
    comp.set_threshold_db(-20.f);
    comp.set_ratio(4.f);
    comp.set_makeup_gain_db(3.f);

    float                                 env = 0.f; // mirror of the internal envelope follower
    std::mt19937                          rng(99);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    const float                           attack  = 1.f - std::exp(-1.f / (0.005f * 48000.f));
    const float                           release = 1.f - std::exp(-1.f / (0.1f * 48000.f));
    for (int i = 0; i < 20000; ++i) {
        const float w    = dist(rng);
        const float gain = comp.process_envelope(w);

        const float abs_w = std::abs(w);
        env += (abs_w - env) * (abs_w > env ? attack : release);
        if (env < 1e-30f) env = 0.f;
        const float env_db    = 20.f * std::log10(std::max(env, 1e-12f));
        const float over      = env_db + 20.f;
        const float reduce_db = (over > 0.f) ? -over * (1.f - 1.f / 4.f) : 0.f;
        const float expected  = std::pow(10.f, (reduce_db + 3.f) / 20.f);
        ASSERT_NEAR(gain, expected, expected * 5e-5f) << "sample " << i;
    }
}

// ---------------------------------------------------------------------------
// Float32 convolver: must match the double convolver to float precision.
// ---------------------------------------------------------------------------

TEST(Convolver32, MatchesDoubleConvolver) {
    std::mt19937                          rng(7);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);

    std::vector<float> ir(139); // odd length: exercises the partial partition
    for (auto& v : ir) v = dist(rng);

    constexpr size_t        block = 64;
    partitioned_convolver   ref(block, ir.data(), ir.size());
    partitioned_convolver32 f32(block, ir.data(), ir.size());
    EXPECT_EQ(f32.num_partitions(), ref.num_partitions());

    std::vector<float> in(block), out_ref(block), out_f32(block);
    for (int b = 0; b < 32; ++b) {
        for (auto& v : in) v = dist(rng);
        ref.process(in.data(), out_ref.data());
        f32.process(in.data(), out_f32.data());
        for (size_t i = 0; i < block; ++i) {
            ASSERT_NEAR(out_f32[i], out_ref[i], 5e-4f) << "block " << b << " sample " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// matrix_applier: linear crossfade, restart on new key, settled application.
// ---------------------------------------------------------------------------

TEST(MatrixApplier, LinearCrossfadeAndSettle) {
    // 2x2: old = identity, new = 2*identity, constant input (1, 1).
    const float prev[] = {1.f, 0.f, 0.f, 1.f};
    const float mat[]  = {2.f, 0.f, 0.f, 2.f};

    dsp::matrix_applier applier;
    constexpr size_t    n = dsp::matrix_applier::k_fade_samples;

    std::vector<float> in0(2 * n, 1.f);
    std::vector<float> out0(n), out1(n);
    const float*       in[]  = {in0.data(), in0.data() + n};
    float*             out[] = {out0.data(), out1.data()};
    applier.apply(mat, mat, prev, 2, 2, in, out, n, false);

    for (size_t i = 0; i < n; ++i) {
        const float alpha    = (static_cast<float>(i) + 1.f) / static_cast<float>(n);
        const float expected = 1.f + (2.f - 1.f) * alpha;
        ASSERT_NEAR(out0[i], expected, 1e-6f) << i;
        ASSERT_NEAR(out1[i], expected, 1e-6f) << i;
    }

    // Settled: pure new matrix.
    applier.apply(mat, mat, prev, 2, 2, in, out, n, false);
    for (size_t i = 0; i < n; ++i) ASSERT_FLOAT_EQ(out0[i], 2.f);

    // New key restarts the fade (same values, different identity).
    const float mat2[] = {2.f, 0.f, 0.f, 2.f};
    applier.apply(mat2, mat2, prev, 2, 2, in, out, n, false);
    EXPECT_NEAR(out0[0], 1.f + 1.f / static_cast<float>(n), 1e-6f);
}

// ---------------------------------------------------------------------------
// binaural_core: prepare validation + equivalence with a per-channel
// double-convolver reference sum.
// ---------------------------------------------------------------------------

TEST(BinauralCore, RejectsBadPrepare) {
    dsp::binaural_core core(1);
    const float*       null_firs[4] = {nullptr, nullptr, nullptr, nullptr};
    EXPECT_FALSE(core.prepare(63, null_firs, null_firs, 8)); // not a power of two
    EXPECT_FALSE(core.prepare(64, nullptr, nullptr, 8));
    EXPECT_FALSE(core.prepare(64, null_firs, null_firs, 0));
    EXPECT_FALSE(core.prepare(64, null_firs, null_firs, 8)); // null FIR pointers
    EXPECT_FALSE(core.is_prepared());
}

TEST(BinauralCore, MatchesDoubleConvolverReference) {
    constexpr int    order = 3;
    constexpr size_t block = 64;
    const size_t     C     = channel_count(order);

    std::vector<const float*> left(C), right(C);
    for (size_t ch = 0; ch < C; ++ch) {
        left[ch]  = builtin_hrtf_left[ch];
        right[ch] = builtin_hrtf_right[ch];
    }

    dsp::binaural_core core(order);
    ASSERT_TRUE(core.prepare(block, left.data(), right.data(), builtin_hrtf_length));
    EXPECT_EQ(core.block_size(), block);

    std::vector<partitioned_convolver> ref_l, ref_r;
    for (size_t ch = 0; ch < C; ++ch) {
        ref_l.emplace_back(block, left[ch], builtin_hrtf_length);
        ref_r.emplace_back(block, right[ch], builtin_hrtf_length);
    }

    std::mt19937                          rng(11);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<std::vector<float>>       hoa(C, std::vector<float>(block));
    std::vector<const float*>             in;
    for (auto& b : hoa) in.push_back(b.data());

    std::vector<float> out_l(block), out_r(block), tmp(block);
    std::vector<float> exp_l(block), exp_r(block);
    for (int b = 0; b < 16; ++b) {
        for (auto& buf : hoa)
            for (auto& v : buf) v = dist(rng);

        std::fill(exp_l.begin(), exp_l.end(), 0.f);
        std::fill(exp_r.begin(), exp_r.end(), 0.f);
        for (size_t ch = 0; ch < C; ++ch) {
            ref_l[ch].process(in[ch], tmp.data());
            for (size_t i = 0; i < block; ++i) exp_l[i] += tmp[i];
            ref_r[ch].process(in[ch], tmp.data());
            for (size_t i = 0; i < block; ++i) exp_r[i] += tmp[i];
        }

        core.process(in.data(), out_l.data(), out_r.data(), block);
        for (size_t i = 0; i < block; ++i) {
            ASSERT_NEAR(out_l[i], exp_l[i], 2e-4f) << "block " << b;
            ASSERT_NEAR(out_r[i], exp_r[i], 2e-4f) << "block " << b;
        }
    }
}
