/// AmbiTap: target-independent ambisonics library
/// Tests for the Ooura FFT wrapper, partitioned convolver, and HRTF dataset.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/math/binaural/convolution.h"
#include "ambitap/math/binaural/hrtf_data.h"
#include "ambitap/math/binaural/ooura_fft.h"
#include "ambitap/math/core/indexing.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

using namespace ambitap;

namespace {

std::vector<float> random_signal(size_t length, unsigned seed) {
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float>                    v(length);
    for (auto& x : v) x = dist(rng);
    return v;
}

std::vector<float> direct_convolution(const std::vector<float>& x, const std::vector<float>& h) {
    std::vector<float> y(x.size() + h.size() - 1, 0.0f);
    for (size_t i = 0; i < x.size(); ++i) {
        for (size_t j = 0; j < h.size(); ++j) {
            y[i + j] += x[i] * h[j];
        }
    }
    return y;
}

} // namespace

TEST(RealFft, RoundTrip) {
    constexpr size_t N = 256;
    real_fft         fft(N);
    EXPECT_EQ(fft.size(), N);
    EXPECT_EQ(fft.num_bins(), N / 2 + 1);

    const auto         input = random_signal(N, 1);
    std::vector<float> freq(N), time(N);
    fft.forward(input.data(), freq.data());
    fft.inverse(freq.data(), time.data());

    for (size_t i = 0; i < N; ++i) {
        EXPECT_NEAR(time[i], input[i], 1e-5f) << "i=" << i;
    }
}

TEST(RealFft, DcBinOfConstantSignal) {
    constexpr size_t   N = 64;
    real_fft           fft(N);
    std::vector<float> input(N, 0.5f), freq(N);
    fft.forward(input.data(), freq.data());

    EXPECT_NEAR(freq[0], 0.5f * static_cast<float>(N), 1e-4f); // DC = sum
    // All non-DC bins of a constant are zero (freq[1] is the Nyquist bin).
    for (size_t i = 1; i < N; ++i) {
        EXPECT_NEAR(freq[i], 0.0f, 1e-4f) << "i=" << i;
    }
}

TEST(PartitionedConvolver, DeltaIrIsPassthrough) {
    constexpr size_t block = 64;
    const float      delta = 1.0f;
    partitioned_convolver conv(block, &delta, 1);

    for (unsigned seed = 0; seed < 4; ++seed) {
        const auto         input = random_signal(block, 100 + seed);
        std::vector<float> output(block);
        conv.process(input.data(), output.data());
        for (size_t i = 0; i < block; ++i) {
            EXPECT_NEAR(output[i], input[i], 1e-5f) << "seed=" << seed << " i=" << i;
        }
    }
}

TEST(PartitionedConvolver, MatchesDirectConvolution) {
    constexpr size_t block     = 64;
    constexpr size_t ir_len    = 200; // spans 4 partitions, last one partial
    constexpr size_t num_blocks = 8;

    const auto ir    = random_signal(ir_len, 7);
    const auto input = random_signal(block * num_blocks, 8);
    const auto ref   = direct_convolution(input, ir);

    partitioned_convolver conv(block, ir.data(), ir.size());
    EXPECT_EQ(conv.num_partitions(), (ir_len + block - 1) / block);

    std::vector<float> output(block);
    for (size_t b = 0; b < num_blocks; ++b) {
        conv.process(input.data() + b * block, output.data());
        for (size_t i = 0; i < block; ++i) {
            EXPECT_NEAR(output[i], ref[b * block + i], 1e-4f) << "block=" << b << " i=" << i;
        }
    }
}

TEST(PartitionedConvolver, SilenceInSilenceOut) {
    constexpr size_t block = 128;
    const auto       ir    = random_signal(300, 9);

    partitioned_convolver conv(block, ir.data(), ir.size());
    std::vector<float>    input(block, 0.0f), output(block, 1.0f);
    for (int b = 0; b < 5; ++b) {
        conv.process(input.data(), output.data());
        for (size_t i = 0; i < block; ++i) {
            EXPECT_EQ(output[i], 0.0f) << "block=" << b << " i=" << i;
        }
    }
}

TEST(PartitionedConvolver, ResetClearsHistoryKeepsIr) {
    constexpr size_t block = 64;
    const auto       ir    = random_signal(128, 10);
    const auto       input = random_signal(block, 11);

    partitioned_convolver conv(block, ir.data(), ir.size());
    std::vector<float>    first(block), second(block);
    conv.process(input.data(), first.data());
    conv.reset();
    conv.process(input.data(), second.data());

    for (size_t i = 0; i < block; ++i) {
        EXPECT_NEAR(second[i], first[i], 1e-6f) << "i=" << i;
    }
}

TEST(HrtfData, DatasetShapeAndContent) {
    EXPECT_EQ(builtin_hrtf_channels, channel_count(builtin_hrtf_order));
    EXPECT_EQ(builtin_hrtf_sample_rate, 44100.0f);

    // Every dataset variant must carry energy in the omni (W) channel.
    auto energy = [](const float (*data)[builtin_hrtf_length]) {
        double e = 0.0;
        for (size_t i = 0; i < builtin_hrtf_length; ++i) {
            e += static_cast<double>(data[0][i]) * static_cast<double>(data[0][i]);
        }
        return e;
    };
    EXPECT_GT(energy(builtin_hrtf_left), 1e-6);
    EXPECT_GT(energy(builtin_hrtf_right), 1e-6);
    EXPECT_GT(energy(builtin_hrtf_magls_left), 1e-6);
    EXPECT_GT(energy(builtin_hrtf_magls_right), 1e-6);
}

// Audit finding B6: the MagLS dataset shipped by an earlier revision of
// scripts/generate_hrtf.py was time-aliased — ~36% of its energy landed
// before the acoustic onset (circular wrap from a spectrum inconsistent with
// a compact causal FIR), audible as pre-echo. A correctly designed dataset
// (single phase-continued projection per bin) keeps pre-onset energy small.
// The embedded dataset was regenerated from the MIT KEMAR source SOFA with
// the corrected generator (phase-trend extrapolation + causality gate).
TEST(HrtfData, MaglsDatasetIsCausal) {
    // The KEMAR ear-canal onset sits near sample 29 at 44.1 kHz; anything in
    // the first 26 samples is pre-onset. The LS dataset satisfies this with
    // 0.0% today; allow a small MagLS allowance for phase relaxation.
    auto pre_onset_fraction = [](const float (*data)[builtin_hrtf_length]) {
        double pre = 0.0, total = 0.0;
        for (size_t ch = 0; ch < builtin_hrtf_channels; ++ch) {
            for (size_t i = 0; i < builtin_hrtf_length; ++i) {
                const double v = data[ch][i];
                total += v * v;
                if (i < 26) pre += v * v;
            }
        }
        return pre / total;
    };
    EXPECT_LT(pre_onset_fraction(builtin_hrtf_left), 0.01);
    EXPECT_LT(pre_onset_fraction(builtin_hrtf_magls_left), 0.05);
    EXPECT_LT(pre_onset_fraction(builtin_hrtf_magls_right), 0.05);
}
