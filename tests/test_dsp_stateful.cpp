/// AmbiTap: target-independent ambisonics library
/// Tests for dsp::doppler, dsp::spatial_compressor, and analysis::energy_vector.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/analysis/energy_vector.h"
#include "ambitap/dsp/doppler.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/spatial_compressor.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace ambitap;

TEST(DspDoppler, SilentUntilPrepared) {
    dsp::doppler dop(1);
    EXPECT_FALSE(dop.is_prepared());

    float in[4] = {1.f, 1.f, 1.f, 1.f};
    float out[4] = {9.f, 9.f, 9.f, 9.f};
    dop.process_frame(in, out);
    for (float v : out) EXPECT_EQ(v, 0.f);
}

TEST(DspDoppler, IntegerDelayPassesImpulse) {
    dsp::doppler dop(1);
    // 343 m at 343 m/s = 1 s = 48000 samples > default 50 m buffer; use 0.1 s.
    // Distance changes glide (Doppler slew); setting it before prepare() makes
    // prepare() snap the delay so the impulse timing below is exact.
    dop.set_distance(34.3f); // 0.1 s -> 4800 samples
    dop.prepare(48000.f);
    ASSERT_TRUE(dop.is_prepared());
    EXPECT_NEAR(dop.current_delay_samples(), 4800.f, 0.01f);

    const size_t delay = 4800;
    float        in[4], out[4];
    for (size_t i = 0; i < delay + 8; ++i) {
        const float v = (i == 0) ? 1.f : 0.f;
        for (auto& x : in) x = v;
        dop.process_frame(in, out);
        const float expected = (i == delay) ? 1.f : 0.f;
        for (size_t ch = 0; ch < 4; ++ch) {
            ASSERT_NEAR(out[ch], expected, 1e-6f) << "i=" << i << " ch=" << ch;
        }
    }
}

TEST(DspDoppler, DelayClampsToBuffer) {
    dsp::doppler dop(1);
    dop.set_max_distance(1.f); // tiny buffer
    dop.prepare(48000.f);
    dop.set_distance(1000.f); // way beyond the buffer
    const float max_allowed = 1.f / 343.f * 48000.f + 2.f;
    EXPECT_LE(dop.current_delay_samples(), max_allowed);
}

TEST(DspSpatialCompressor, QuietSignalGetsOnlyMakeup) {
    dsp::spatial_compressor comp(1);
    comp.prepare(48000.f);
    comp.set_makeup_gain_db(6.f);

    // Envelope is 0 -> far below threshold -> gain is the makeup alone.
    const float gain = comp.process_envelope(0.f);
    EXPECT_NEAR(gain, std::pow(10.f, 6.f / 20.f), 1e-4f);
}

TEST(DspSpatialCompressor, SteadyStateGainReduction) {
    dsp::spatial_compressor comp(1);
    comp.prepare(48000.f);
    comp.set_threshold_db(-12.f);
    comp.set_ratio(4.f);

    // Drive with a constant W of 1.0 (0 dB) until the envelope converges.
    float gain = 1.f;
    for (int i = 0; i < 48000; ++i) gain = comp.process_envelope(1.f);

    // 12 dB over threshold at 4:1 -> reduce by 12 * (1 - 1/4) = 9 dB.
    EXPECT_NEAR(gain, std::pow(10.f, -9.f / 20.f), 1e-3f);
}

TEST(DspSpatialCompressor, GainAppliesUniformlyAcrossChannels) {
    dsp::spatial_compressor comp(2);
    comp.prepare(48000.f);

    constexpr size_t   frames = 64;
    const size_t       C      = comp.channels();
    std::vector<std::vector<float>> in(C, std::vector<float>(frames, 0.5f)),
        out(C, std::vector<float>(frames));
    std::vector<const float*> ip;
    std::vector<float*>       op;
    for (size_t ch = 0; ch < C; ++ch) {
        ip.push_back(in[ch].data());
        op.push_back(out[ch].data());
    }
    comp.process(ip.data(), op.data(), frames);

    // Spatial preservation: every channel scaled identically per frame.
    for (size_t i = 0; i < frames; ++i) {
        for (size_t ch = 1; ch < C; ++ch) {
            EXPECT_FLOAT_EQ(out[ch][i], out[0][i]) << "i=" << i << " ch=" << ch;
        }
    }
}

TEST(AnalysisEnergyVector, ConvergesToSourceDirection) {
    analysis::energy_vector ev;
    ev.prepare(48000.f);
    ev.set_smoothing_time(0.001f);

    // A constant source encoded at the front: W = X = 1, Y = Z = 0.
    dsp::encoder enc(1);
    enc.set_direction(0.f, 0.f);
    enc.snap_parameters();
    float frame[4], out[3] = {};
    enc.process_sample(1.f, frame);

    for (int i = 0; i < 48000; ++i) ev.process_frame(frame, out);

    EXPECT_NEAR(out[0], 1.f, 1e-3f); // x (front)
    EXPECT_NEAR(out[1], 0.f, 1e-3f); // y
    EXPECT_NEAR(out[2], 0.f, 1e-3f); // z

    // Left-pointing source: azimuth pi/2 -> y axis.
    enc.set_direction(static_cast<float>(M_PI) * 0.5f, 0.f);
    enc.snap_parameters();
    enc.process_sample(1.f, frame);
    ev.reset();
    for (int i = 0; i < 48000; ++i) ev.process_frame(frame, out);
    EXPECT_NEAR(out[0], 0.f, 1e-3f);
    EXPECT_NEAR(out[1], 1.f, 1e-3f);
    EXPECT_NEAR(out[2], 0.f, 1e-3f);
}
