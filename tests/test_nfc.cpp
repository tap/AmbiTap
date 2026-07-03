/// AmbiTap: target-independent ambisonics library
/// Tests for dsp::nfc — near-field compensation (NFC-HOA, Daniel).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/dsp/nfc.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace ambitap;

namespace {

    /// Planar buffers + pointer tables for process().
    struct planar {
        std::vector<std::vector<float>> in_bufs, out_bufs;
        std::vector<const float*>       in;
        std::vector<float*>             out;
        planar(size_t channels, size_t frames)
            : in_bufs(channels, std::vector<float>(frames, 0.f))
            , out_bufs(channels, std::vector<float>(frames, 0.f)) {
            for (size_t ch = 0; ch < channels; ++ch) {
                in.push_back(in_bufs[ch].data());
                out.push_back(out_bufs[ch].data());
            }
        }
    };

} // namespace

TEST(DspNfc, IdentityAtEqualDistances) {
    // At r_src == r_ref every section's zero coincides with its pole and the
    // digital coefficients reduce to a passthrough (b0 = 1, b1 = a1, b2 = a2).
    // The cancellation is not bit-exact under floating-point contraction
    // (AppleClang/arm64 fuses b1*x - a1*y into an FMA by default), and the
    // near-unit-circle poles of these low-corner sections amplify any such
    // residue — with float32 state that reached ~-65 dB, which is why the
    // recurrence runs in double (see nfc.h). Assert a tight absolute bound:
    // double state keeps the residue near -240 dB, so 1e-6 has huge margin
    // while still failing loudly if the state precision ever regresses.
    dsp::nfc filt(3);
    filt.prepare(48000.f);
    filt.set_source_distance(2.5f);
    filt.set_reference_distance(2.5f);
    filt.snap_parameters();

    constexpr size_t frames = 512;
    planar           io(filt.channels(), frames);
    for (size_t ch = 0; ch < filt.channels(); ++ch) {
        for (size_t i = 0; i < frames; ++i) {
            io.in_bufs[ch][i] =
                std::sin(0.01f * static_cast<float>(i + ch)) + 0.25f * static_cast<float>(ch);
        }
    }
    filt.process(io.in.data(), io.out.data(), frames);

    for (size_t ch = 0; ch < filt.channels(); ++ch) {
        for (size_t i = 0; i < frames; ++i) {
            ASSERT_NEAR(io.out_bufs[ch][i], io.in_bufs[ch][i], 1e-6f) << "ch=" << ch << " i=" << i;
        }
    }
}

TEST(DspNfc, DcGainMatchesClosedForm) {
    // The order-m filter's DC gain has the closed form (r_ref / r_src)^m:
    // a low shelf cut for sources beyond the reference distance, a boost
    // inside it. Drive with DC and compare the steady state per channel.
    struct scenario {
        float rs, rref;
        int   order;
    };
    const scenario cases[] = {
        {2.0f, 1.0f, 5},        // cut: source beyond the reference
        {0.5f, 1.0f, 5},        // boost: source inside the reference
        {1.5f, 1.0f, max_order} // exercises the Bessel roots at every order
    };

    for (const auto& sc : cases) {
        dsp::nfc filt(sc.order);
        filt.prepare(48000.f);
        filt.set_source_distance(sc.rs);
        filt.set_reference_distance(sc.rref);
        filt.snap_parameters();

        const size_t       C = filt.channels();
        std::vector<float> in(C, 1.f), out(C, 0.f);
        for (int i = 0; i < 48000; ++i) filt.process_frame(in.data(), out.data());

        // Tolerance: the audio path is float32 (embedded contract), and at
        // these corner frequencies the biquad denominator sum 1 + a1 + a2
        // cancels to ~1e-4, so coefficient quantization bounds the realized
        // DC gain to ~0.3% (0.03 dB) of the closed form.
        for (size_t ch = 0; ch < C; ++ch) {
            const int   m        = acn_order(ch);
            const float expected = std::pow(sc.rref / sc.rs, static_cast<float>(m));
            EXPECT_EQ(expected, filt.dc_gain(m));
            EXPECT_NEAR(out[ch], expected, 1e-2f * expected)
                << "rs=" << sc.rs << " rref=" << sc.rref << " ch=" << ch;
        }
    }
}

TEST(DspNfc, SourceDistanceClampsToDocumentedMinimum) {
    dsp::nfc filt(3);
    filt.set_source_distance(0.001f);
    EXPECT_EQ(filt.source_distance(), dsp::nfc::k_min_distance);
    filt.set_reference_distance(0.f);
    EXPECT_EQ(filt.reference_distance(), dsp::nfc::k_min_distance);
}

TEST(DspNfc, ImpulseResponseDecaysAtAggressiveDistances) {
    // The poles are the theta_m (Bessel) roots scaled by c / r_ref — all in
    // the left half-plane, mapped inside the unit circle by the bilinear
    // transform. Even at the minimum source distance and a far reference the
    // impulse response must stay finite and die away.
    for (int order = 1; order <= 5; ++order) {
        dsp::nfc filt(order);
        filt.prepare(48000.f);
        filt.set_source_distance(0.01f); // clamps to k_min_distance
        filt.set_reference_distance(3.f);
        filt.snap_parameters();

        const size_t       C = filt.channels();
        std::vector<float> in(C, 0.f), out(C, 0.f);
        float              peak = 0.f, tail = 0.f;
        constexpr int      total = 48000, tail_start = 44000;
        for (int i = 0; i < total; ++i) {
            const float v = (i == 0) ? 1.f : 0.f;
            for (auto& x : in) x = v;
            filt.process_frame(in.data(), out.data());
            for (size_t ch = 0; ch < C; ++ch) {
                ASSERT_TRUE(std::isfinite(out[ch])) << "order=" << order << " i=" << i;
                const float a = std::abs(out[ch]);
                peak          = std::max(peak, a);
                if (i >= tail_start) tail = std::max(tail, a);
            }
        }
        EXPECT_GT(peak, 0.f) << "order=" << order;
        EXPECT_LT(tail, 1e-6f * peak) << "order=" << order;
    }
}

TEST(DspNfc, ParameterChangesRampWithoutSnap) {
    // Without snap_parameters() a distance change ramps in over
    // k_smoothing_samples; the first block after the change must neither jump
    // instantly to the new response nor blow up.
    dsp::nfc filt(2);
    filt.prepare(48000.f);
    filt.snap_parameters();

    const size_t       C = filt.channels();
    std::vector<float> in(C, 1.f), out(C, 0.f);
    for (int i = 0; i < 4800; ++i) filt.process_frame(in.data(), out.data());
    const float before = out[C - 1];

    filt.set_source_distance(0.25f); // boost — ramped, not snapped
    filt.process_frame(in.data(), out.data());
    EXPECT_NEAR(out[C - 1], before, 0.25f * std::abs(before) + 1e-3f);

    for (int i = 0; i < 48000; ++i) {
        filt.process_frame(in.data(), out.data());
        for (size_t ch = 0; ch < C; ++ch) ASSERT_TRUE(std::isfinite(out[ch]));
    }
    EXPECT_NEAR(out[C - 1], filt.dc_gain(2), 1e-2f * filt.dc_gain(2));
}
