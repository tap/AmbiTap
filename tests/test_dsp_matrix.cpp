/// AmbiTap: target-independent ambisonics library
/// Tests for dsp::rotator and dsp::decoder (async_rebuilder-based processors).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/decoder.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/rotator.h"
#include "ambitap/math/geometry/layouts.h"

using namespace ambitap;

namespace {

    // Newly published matrices crossfade in over k_fade_samples; run the fade out
    // so subsequent assertions see the settled matrix exactly.
    void run_out_rotator_fade(const dsp::rotator& rot) {
        std::vector<float> in(rot.channels(), 0.f), out(rot.channels());
        for (size_t i = 0; i < dsp::rotator::k_fade_samples; ++i) {
            rot.process_frame(in.data(), out.data());
        }
    }

    void run_out_decoder_fade(const dsp::decoder& dec, size_t out_channels) {
        std::vector<float> in(dec.input_channels(), 0.f), out(out_channels);
        for (size_t i = 0; i < dsp::decoder::k_fade_samples; ++i) {
            dec.process_frame(in.data(), out.data(), out_channels);
        }
    }

} // namespace

TEST(DspRotator, PassthroughUntilFirstSetter) {
    dsp::rotator rot(2);
    EXPECT_FALSE(rot.is_active());

    std::vector<float> in(rot.channels()), out(rot.channels());
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(i) - 3.f;
    rot.process_frame(in.data(), out.data());
    EXPECT_EQ(out, in);
}

TEST(DspRotator, RotatedEncodingMatchesEncodedRotation) {
    // Defining property through the full async path: rotating an encoded
    // source equals encoding at the rotated direction. Yaw-only keeps the
    // direction arithmetic simple: yaw rotates azimuth by +yaw.
    constexpr int   order = 3;
    constexpr float az = 0.4f, el = 0.25f, yaw = 0.9f;

    dsp::rotator rot(order);
    rot.set_rotation(yaw, 0.f, 0.f);
    rot.wait_for_settling();
    ASSERT_TRUE(rot.is_active());
    run_out_rotator_fade(rot);

    dsp::encoder enc(order);
    enc.set_direction(az, el);
    std::vector<float> in(enc.coefficients(), enc.coefficients() + enc.channels());
    std::vector<float> out(rot.channels());
    rot.process_frame(in.data(), out.data());

    enc.set_direction(az + yaw, el);
    for (size_t ch = 0; ch < rot.channels(); ++ch) {
        EXPECT_NEAR(out[ch], enc.coefficients()[ch], 1e-3f) << "ch=" << ch;
    }
}

TEST(DspRotator, IndividualSettersCompose) {
    dsp::rotator rot(1);
    rot.set_yaw(0.5f);
    rot.set_pitch(-0.2f);
    rot.set_roll(0.1f);
    rot.wait_for_settling();
    EXPECT_FLOAT_EQ(rot.yaw(), 0.5f);
    EXPECT_FLOAT_EQ(rot.pitch(), -0.2f);
    EXPECT_FLOAT_EQ(rot.roll(), 0.1f);
    ASSERT_TRUE(rot.is_active());
    EXPECT_EQ(rot.matrix()->size(), rot.channels() * rot.channels());
}

TEST(DspDecoder, SilenceUntilConfigured) {
    dsp::decoder dec(1);
    float        in[4] = {1.f, 0.5f, -0.5f, 0.25f};
    float        out[8];
    dec.process_frame(in, out, 8);
    for (float v : out)
        EXPECT_EQ(v, 0.f);
}

TEST(DspDecoder, MatchesDirectConstruction) {
    constexpr int order    = 1;
    const auto    speakers = layouts::cube();

    dsp::decoder dec(order);
    dec.set_speakers(speakers);
    dec.set_algorithm(dsp::decoder_algorithm::mode_match);
    dec.wait_for_settling();

    auto m = dec.load_matrix();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->speakers, 8u);
    EXPECT_EQ(m->channels, 4u);

    const auto D = compute_mode_matching_decoder(order, speakers);
    for (size_t r = 0; r < m->speakers; ++r) {
        for (size_t c = 0; c < m->channels; ++c) {
            EXPECT_FLOAT_EQ(m->data[r * m->channels + c], D(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)))
                << "r=" << r << " c=" << c;
        }
    }

    // process_frame applies the matrix (after the adoption crossfade).
    run_out_decoder_fade(dec, 8);
    dsp::encoder enc(order);
    enc.set_direction(speakers[2].azimuth, speakers[2].elevation);
    std::vector<float> in(enc.coefficients(), enc.coefficients() + enc.channels());
    float              out[8];
    dec.process_frame(in.data(), out, 8);

    size_t loudest = 0;
    for (size_t s = 1; s < 8; ++s) {
        if (std::fabs(out[s]) > std::fabs(out[loudest])) loudest = s;
    }
    EXPECT_EQ(loudest, 2u);
}

TEST(DspDecoder, EmptySpeakerListKeepsPreviousMatrix) {
    dsp::decoder dec(1);
    dec.set_speakers(layouts::cube());
    dec.wait_for_settling();
    ASSERT_NE(dec.load_matrix(), nullptr);

    dec.set_speakers({});
    dec.wait_for_settling();
    auto m = dec.load_matrix();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->speakers, 8u);
}

TEST(DspDecoder, PublishCallbackFires) {
    std::atomic<int> publishes{0};
    dsp::decoder     dec(1, [&publishes] { ++publishes; });
    dec.set_speakers(layouts::quad());
    dec.wait_for_settling();
    // wait_for_settling guarantees the build finished; the callback runs just
    // after — allow it a moment.
    for (int i = 0; i < 1000 && publishes.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GE(publishes.load(), 1);
}
