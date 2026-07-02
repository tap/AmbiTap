/// AmbiTap: target-independent ambisonics library
/// Tests for dsp::mirror, dsp::format_converter, dsp::virtual_mic, and
/// dsp::directional_loudness.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/dsp/directional_loudness.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/format_converter.h"
#include "ambitap/dsp/mirror.h"
#include "ambitap/dsp/virtual_mic.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace ambitap;

namespace {

std::vector<float> encode_at(int order, float az, float el) {
    dsp::encoder enc(order);
    enc.set_direction(az, el);
    std::vector<float> v(enc.channels());
    for (size_t ch = 0; ch < v.size(); ++ch) v[ch] = enc.coefficients()[ch];
    return v;
}

} // namespace

// Mirroring an encoded source must equal encoding the mirrored direction.
TEST(DspMirror, MirrorsMatchReflectedEncoding) {
    constexpr int   order = 3;
    constexpr float az = 0.7f, el = 0.4f;
    const float     pi = static_cast<float>(M_PI);

    const auto src = encode_at(order, az, el);

    struct Case {
        bool  lr, fb, ud;
        float ref_az, ref_el;
    };
    const Case cases[] = {
        {true, false, false, -az, el},          // LR: az -> -az
        {false, true, false, pi - az, el},      // FB: az -> pi - az
        {false, false, true, az, -el},          // UD: el -> -el
        {true, true, false, az - pi, el},       // LR+FB: az -> az - pi (mod 2pi)
        {true, false, true, -az, -el},          // LR+UD
    };

    for (const auto& c : cases) {
        dsp::mirror m(order);
        m.set_flip_lr(c.lr);
        m.set_flip_fb(c.fb);
        m.set_flip_ud(c.ud);

        std::vector<float> out(m.channels());
        m.process_frame(src.data(), out.data());

        const auto expected = encode_at(order, c.ref_az, c.ref_el);
        for (size_t ch = 0; ch < out.size(); ++ch) {
            EXPECT_NEAR(out[ch], expected[ch], 1e-5f)
                << "lr=" << c.lr << " fb=" << c.fb << " ud=" << c.ud << " ch=" << ch;
        }
    }
}

TEST(DspMirror, NoFlipsIsIdentity) {
    dsp::mirror m(2);
    for (size_t ch = 0; ch < m.channels(); ++ch) {
        EXPECT_FLOAT_EQ(m.channel_sign(ch), 1.0f);
    }
}

TEST(DspFormatConverter, RoundTripIsIdentity) {
    constexpr int order = 3;
    const auto    src   = encode_at(order, 1.1f, -0.4f);

    dsp::format_converter to_fuma(order);
    to_fuma.set_direction(dsp::format_direction::ambix_to_fuma);
    dsp::format_converter to_ambix(order);
    to_ambix.set_direction(dsp::format_direction::fuma_to_ambix);

    std::vector<float> fuma(src.size()), back(src.size());
    to_fuma.process_frame(src.data(), fuma.data());
    to_ambix.process_frame(fuma.data(), back.data());

    for (size_t ch = 0; ch < src.size(); ++ch) {
        EXPECT_NEAR(back[ch], src[ch], 1e-5f) << "ch=" << ch;
    }
}

TEST(DspFormatConverter, WChannelGain) {
    // FuMa W = SN3D W / sqrt(2); both directions place W at index 0.
    dsp::format_converter conv(1);
    EXPECT_EQ(conv.input_index(0), 0u);
    EXPECT_NEAR(conv.input_gain(0), 1.0f / std::sqrt(2.0f), 1e-6f);

    conv.set_direction(dsp::format_direction::fuma_to_ambix);
    EXPECT_NEAR(conv.input_gain(0), std::sqrt(2.0f), 1e-6f);

    // Order 1: FuMa is W X Y Z; ACN is W Y Z X — X sits at ACN 3, FuMa 1.
    EXPECT_EQ(conv.input_index(3), 1u);
    EXPECT_NEAR(conv.input_gain(3), 1.0f, 1e-6f);
}

TEST(DspVirtualMic, FirstOrderCardioid) {
    dsp::virtual_mic mic(1);
    mic.set_direction(0.9f, 0.3f);

    // Source at the look direction: W*W + dir·dir = 1 + 1 = 2.
    const auto at_look = encode_at(1, 0.9f, 0.3f);
    EXPECT_NEAR(mic.process_frame(at_look.data()), 2.0f, 1e-5f);

    // Source at the antipode: 1 - 1 = 0 (cardioid null).
    const float pi      = static_cast<float>(M_PI);
    const auto  at_back = encode_at(1, 0.9f - pi, -0.3f);
    EXPECT_NEAR(mic.process_frame(at_back.data()), 0.0f, 1e-5f);
}

TEST(DspVirtualMic, MaxReAppliesPerOrderWeights) {
    dsp::virtual_mic mic(3);
    mic.set_direction(0.2f, -0.1f);

    std::vector<float> plain(mic.coefficients(), mic.coefficients() + mic.channels());
    mic.set_max_re(true);

    const auto weights = max_re_weights(3);
    for (size_t ch = 0; ch < mic.channels(); ++ch) {
        const auto n = static_cast<size_t>(acn_order(ch));
        EXPECT_NEAR(mic.coefficient(ch), plain[ch] * weights[n], 1e-6f) << "ch=" << ch;
    }
}

TEST(DspDirectionalLoudness, UnityGainIsBypass) {
    dsp::directional_loudness dl(2);
    dl.set_direction(0.5f, 0.25f);

    const auto         in = encode_at(2, -0.8f, 0.6f);
    std::vector<float> out(dl.channels());
    dl.process_frame(in.data(), out.data());

    for (size_t ch = 0; ch < out.size(); ++ch) {
        EXPECT_FLOAT_EQ(out[ch], in[ch]) << "ch=" << ch;
    }
}

// A source encoded exactly at the look direction must come out scaled by
// exactly the requested gain — the property the beam calibration exists for.
// (Audit finding C4: the uncalibrated formula achieved 1 + (g-1)(order+1),
// turning "attenuate to 0.5" into an inverted, louder -1.0.)
TEST(DspDirectionalLoudness, AchievesRequestedGainAtLookDirection) {
    constexpr float az = 0.3f, el = -0.2f;
    for (int order : {1, 3, 5}) {
        const auto in = encode_at(order, az, el);
        for (float gain : {0.0f, 0.5f, 1.0f, 2.0f}) {
            dsp::directional_loudness dl(order);
            dl.set_direction(az, el);
            dl.set_gain(gain);

            std::vector<float> out(dl.channels());
            dl.process_frame(in.data(), out.data());

            for (size_t ch = 0; ch < out.size(); ++ch) {
                EXPECT_NEAR(out[ch], gain * in[ch], 1e-5f)
                    << "order=" << order << " gain=" << gain << " ch=" << ch;
            }
        }
    }
}

// Sources well away from the look direction are far less affected than the
// on-beam source: attenuating the beam must not gut the rest of the scene.
TEST(DspDirectionalLoudness, OffBeamSourceLargelyUnaffected) {
    constexpr int order = 3;
    dsp::directional_loudness dl(order);
    dl.set_direction(0.0f, 0.0f); // look at front
    dl.set_gain(0.0f);            // remove the front

    const auto         in = encode_at(order, static_cast<float>(M_PI), 0.0f); // rear source
    std::vector<float> out(dl.channels());
    dl.process_frame(in.data(), out.data());

    float in_e = 0.f, diff_e = 0.f;
    for (size_t ch = 0; ch < out.size(); ++ch) {
        in_e += in[ch] * in[ch];
        diff_e += (out[ch] - in[ch]) * (out[ch] - in[ch]);
    }
    EXPECT_LT(diff_e, 0.05f * in_e);
}
