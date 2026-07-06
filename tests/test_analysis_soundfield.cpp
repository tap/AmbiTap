/// @file test_analysis_soundfield.cpp
/// @brief Tests for analysis::soundfield_grid.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/analysis/soundfield_grid.h"
#include "ambitap/dsp/encoder.h"

using namespace ambitap;

TEST(AnalysisSoundfieldGrid, DimensionsAndResolutionChange) {
    analysis::soundfield_grid sg(1, 32);
    EXPECT_EQ(sg.azimuth_steps(), 32);
    EXPECT_EQ(sg.elevation_steps(), 16);

    auto img = sg.snapshot(40.f);
    EXPECT_EQ(img.rows, 16);
    EXPECT_EQ(img.cols, 32);
    EXPECT_EQ(img.data.size(), 512u);
    for (float v : img.data) {
        EXPECT_GE(v, 0.f);
        EXPECT_LE(v, 1.f);
    }

    sg.set_azimuth_steps(16);
    EXPECT_EQ(sg.azimuth_steps(), 16);
    EXPECT_EQ(sg.snapshot(40.f).data.size(), 128u);
}

TEST(AnalysisSoundfieldGrid, LocalizesEncodedSource) {
    constexpr int    order    = 3;
    constexpr int    az_steps = 32;
    constexpr size_t frames   = 64;

    analysis::soundfield_grid sg(order, az_steps);
    sg.prepare(48000.f);
    sg.set_smoothing_time_ms(1.f); // converge fast for the test

    // Source at an exact grid cell: front (az = 0 -> col 16), horizon
    // (el = 0 -> row 8 of 16).
    const float az = analysis::soundfield_grid::azimuth_of_column(16, az_steps);
    const float el = analysis::soundfield_grid::elevation_of_row(8, az_steps / 2);
    EXPECT_NEAR(az, 0.f, 1e-6f);
    EXPECT_NEAR(el, 0.f, 1e-6f);

    dsp::encoder enc(order);
    enc.set_direction(az, el);

    // Constant source: every channel holds its SH coefficient.
    std::vector<std::vector<float>> bufs(sg.channels(), std::vector<float>(frames));
    std::vector<const float*>       ptrs;
    for (size_t c = 0; c < sg.channels(); ++c) {
        std::fill(bufs[c].begin(), bufs[c].end(), enc.coefficients()[c]);
        ptrs.push_back(bufs[c].data());
    }

    for (int b = 0; b < 200; ++b) {
        sg.process(ptrs.data(), frames);
    }

    const auto img    = sg.snapshot(40.f);
    size_t     argmax = 0;
    for (size_t d = 1; d < img.data.size(); ++d) {
        if (img.data[d] > img.data[argmax]) {
            argmax = d;
        }
    }
    EXPECT_EQ(argmax, 8u * az_steps + 16u);
    EXPECT_FLOAT_EQ(img.data[argmax], 1.f); // the peak normalizes to exactly 1
    EXPECT_GT(img.peak_db, -20.f);          // a real signal, not the silence floor
}

TEST(AnalysisSoundfieldGrid, SilenceSnapshotsAtFloor) {
    analysis::soundfield_grid sg(1, 16);
    const auto                img = sg.snapshot(40.f);
    // All energies identical (zero) -> everything normalizes to 1 at a very
    // low absolute peak.
    EXPECT_NEAR(img.peak_db, -120.f, 1.f);
}
