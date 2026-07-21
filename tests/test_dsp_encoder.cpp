/// @file test_dsp_encoder.cpp
/// @brief Tests for the dsp::encoder processor.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/encoder.h"
#include "ambitap/math/core/spherical_harmonics.h"

using namespace tap::ambi;

TEST(DspEncoder, CoefficientsMatchEvaluateSh) {
    dsp::encoder enc(3);
    EXPECT_EQ(enc.order(), 3);
    EXPECT_EQ(enc.channels(), 16u);

    enc.set_direction(0.7f, -0.3f);

    float expected[k_max_channel_count];
    evaluate_sh(3, 0.7f, -0.3f, expected);
    for (size_t ch = 0; ch < enc.channels(); ++ch) {
        EXPECT_FLOAT_EQ(enc.coefficients()[ch], expected[ch]) << "ch=" << ch;
    }
}

TEST(DspEncoder, IndividualAngleSettersRecalculate) {
    dsp::encoder enc(2);
    enc.set_azimuth(1.1f);
    enc.set_elevation(0.4f);

    float expected[k_max_channel_count];
    evaluate_sh(2, 1.1f, 0.4f, expected);
    for (size_t ch = 0; ch < enc.channels(); ++ch) {
        EXPECT_FLOAT_EQ(enc.coefficients()[ch], expected[ch]) << "ch=" << ch;
    }
}

TEST(DspEncoder, ProcessAppliesCoefficientsAndGain) {
    dsp::encoder enc(1);
    enc.set_direction(0.5f, 0.2f);
    enc.set_gain(2.0f);
    enc.snap_parameters(); // parameter changes ramp; test wants exact values

    constexpr size_t                frames = 8;
    std::vector<float>              in     = {1.0f, -0.5f, 0.25f, 0.0f, 0.75f, -1.0f, 0.1f, 0.9f};
    std::vector<std::vector<float>> out(enc.channels(), std::vector<float>(frames));
    std::vector<float*>             ptrs;
    for (auto& v : out) {
        ptrs.push_back(v.data());
    }

    enc.process(in.data(), ptrs.data(), frames);

    for (size_t ch = 0; ch < enc.channels(); ++ch) {
        for (size_t i = 0; i < frames; ++i) {
            EXPECT_FLOAT_EQ(out[ch][i], in[i] * enc.coefficients()[ch] * 2.0f) << "ch=" << ch << " i=" << i;
        }
    }

    // channel_gain and process_sample agree with process().
    float sample_out[4];
    enc.process_sample(in[0], sample_out);
    for (size_t ch = 0; ch < enc.channels(); ++ch) {
        EXPECT_FLOAT_EQ(sample_out[ch], out[ch][0]);
        EXPECT_FLOAT_EQ(enc.channel_gain(ch), enc.coefficients()[ch] * 2.0f);
    }
}

TEST(DspEncoder, DefaultDirectionIsFront) {
    // At (0, 0) the W and X channels are 1, Y and Z are 0 (order 1, SN3D).
    dsp::encoder enc(1);
    EXPECT_FLOAT_EQ(enc.coefficients()[0], 1.0f);
    EXPECT_FLOAT_EQ(enc.coefficients()[1], 0.0f);
    EXPECT_FLOAT_EQ(enc.coefficients()[2], 0.0f);
    EXPECT_FLOAT_EQ(enc.coefficients()[3], 1.0f);
}
