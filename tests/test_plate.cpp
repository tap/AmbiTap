/// @file test_plate.cpp
/// @brief Tests for dsp::plate — the N-in/M-out Dattorro/Griesinger plate tank.
///        Impulse-response gates (decay monotonicity, damping, decorrelation),
///        block-size invariance, lifecycle, and a TSan-covered setter race.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/plate.h"

using namespace tap::ambi;

namespace {

    constexpr float  k_fs    = 32000.0f;
    constexpr size_t k_block = 64;

    /// Render the impulse response of a prepared-from-scratch plate:
    /// prepare -> snap -> unit impulse into input 0 -> process in blocks.
    std::vector<std::vector<float>> render_ir(dsp::plate& p, double seconds, size_t impulse_input = 0) {
        p.prepare(k_fs);
        p.snap_parameters();

        const size_t inputs  = p.inputs();
        const size_t outputs = p.outputs();
        const size_t samples = static_cast<size_t>(seconds * k_fs + 0.5);
        const size_t blocks  = (samples + k_block - 1) / k_block;

        std::vector<std::vector<float>> in(inputs, std::vector<float>(k_block, 0.0f));
        std::vector<std::vector<float>> out(outputs, std::vector<float>(k_block));
        std::vector<const float*>       in_ptrs;
        std::vector<float*>             out_ptrs;
        for (auto& b : in) {
            in_ptrs.push_back(b.data());
        }
        for (auto& b : out) {
            out_ptrs.push_back(b.data());
        }

        std::vector<std::vector<float>> ir(outputs, std::vector<float>(blocks * k_block, 0.0f));
        for (size_t bi = 0; bi < blocks; ++bi) {
            for (auto& b : in) {
                std::fill(b.begin(), b.end(), 0.0f);
            }
            if (bi == 0) {
                in[impulse_input][0] = 1.0f;
            }
            p.process(in_ptrs.data(), out_ptrs.data(), k_block);
            for (size_t ch = 0; ch < outputs; ++ch) {
                std::copy(out[ch].begin(), out[ch].end(), ir[ch].begin() + static_cast<std::ptrdiff_t>(bi * k_block));
            }
        }
        for (auto& ch : ir) {
            ch.resize(samples);
        }
        return ir;
    }

    /// Sum of squares over [from, to) seconds.
    double window_energy(const std::vector<float>& x, double from, double to) {
        const size_t a = static_cast<size_t>(from * k_fs);
        const size_t b = std::min(static_cast<size_t>(to * k_fs), x.size());
        double       e = 0.0;
        for (size_t i = a; i < b; ++i) {
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        }
        return e;
    }

    /// Crude high-frequency proxy: first-difference energy over total energy.
    double hf_fraction(const std::vector<float>& x, double from, double to) {
        const size_t a  = static_cast<size_t>(from * k_fs);
        const size_t b  = std::min(static_cast<size_t>(to * k_fs), x.size());
        double       ed = 0.0, e = 0.0;
        for (size_t i = a + 1; i < b; ++i) {
            const double d = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
            ed += d * d;
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        }
        return e > 0.0 ? ed / e : 0.0;
    }

    /// Zero-lag normalized cross-correlation over [from, to) seconds.
    double correlation(const std::vector<float>& x, const std::vector<float>& y, double from, double to) {
        const size_t a  = static_cast<size_t>(from * k_fs);
        const size_t b  = std::min({static_cast<size_t>(to * k_fs), x.size(), y.size()});
        double       xy = 0.0, xx = 0.0, yy = 0.0;
        for (size_t i = a; i < b; ++i) {
            xy += static_cast<double>(x[i]) * static_cast<double>(y[i]);
            xx += static_cast<double>(x[i]) * static_cast<double>(x[i]);
            yy += static_cast<double>(y[i]) * static_cast<double>(y[i]);
        }
        return (xx > 0.0 && yy > 0.0) ? xy / std::sqrt(xx * yy) : 0.0;
    }

} // namespace

TEST(Plate, SilentUntilPrepared) {
    dsp::plate p(2, 2, 2);

    std::array<float, 2> frame_in{1.0f, -1.0f};
    std::array<float, 2> frame_out{9.0f, 9.0f};
    p.process_frame(frame_in.data(), frame_out.data());
    EXPECT_EQ(frame_out[0], 0.0f);
    EXPECT_EQ(frame_out[1], 0.0f);

    std::vector<float> in0(k_block, 1.0f), in1(k_block, 1.0f);
    std::vector<float> out0(k_block, 9.0f), out1(k_block, 9.0f);
    const float*       ins[2]  = {in0.data(), in1.data()};
    float*             outs[2] = {out0.data(), out1.data()};
    p.process(ins, outs, k_block);
    for (size_t i = 0; i < k_block; ++i) {
        EXPECT_EQ(out0[i], 0.0f);
        EXPECT_EQ(out1[i], 0.0f);
    }
}

TEST(Plate, ImpulseProducesTailOnEveryOutput) {
    dsp::plate p(1, 2, 2);
    p.set_decay(0.7f);
    const auto ir = render_ir(p, 1.0);

    for (size_t ch = 0; ch < 2; ++ch) {
        EXPECT_GT(window_energy(ir[ch], 0.0, 0.25), 0.0) << "output " << ch;
        EXPECT_GT(window_energy(ir[ch], 0.25, 0.75), 0.0) << "output " << ch << " has no tail";
    }
}

TEST(Plate, DecayLengthensTail) {
    auto late_over_early = [](float decay) {
        dsp::plate p(1, 2, 2);
        p.set_decay(decay);
        const auto ir = render_ir(p, 1.0);
        return window_energy(ir[0], 0.6, 0.9) / window_energy(ir[0], 0.1, 0.3);
    };

    const double short_tail = late_over_early(0.4f);
    const double long_tail  = late_over_early(0.8f);
    EXPECT_GT(long_tail, short_tail * 4.0);
}

TEST(Plate, DampingDarkensTail) {
    auto brightness = [](float damping) {
        dsp::plate p(1, 2, 2);
        p.set_decay(0.8f);
        p.set_damping(damping);
        const auto ir = render_ir(p, 0.8);
        return hf_fraction(ir[0], 0.2, 0.7);
    };

    EXPECT_LT(brightness(0.6f), brightness(0.0f) * 0.7);
}

TEST(Plate, OutputsAreMutuallyDecorrelated) {
    dsp::plate p(1, 4, 4);
    p.set_decay(0.85f);
    const auto ir = render_ir(p, 0.8);

    for (size_t a = 0; a < 4; ++a) {
        for (size_t b = a + 1; b < 4; ++b) {
            const double c = correlation(ir[a], ir[b], 0.05, 0.7);
            EXPECT_LT(std::abs(c), 0.5) << "outputs " << a << " and " << b;
        }
    }
}

TEST(Plate, EveryInputReachesEveryOutput) {
    for (size_t input = 0; input < 3; ++input) {
        dsp::plate p(3, 5, 4);
        p.set_decay(0.7f);
        const auto ir = render_ir(p, 0.5, input);
        for (size_t ch = 0; ch < 5; ++ch) {
            EXPECT_GT(window_energy(ir[ch], 0.0, 0.5), 0.0) << "input " << input << " -> output " << ch;
        }
    }
}

TEST(Plate, FreezeSustains) {
    dsp::plate p(1, 2, 2);
    p.set_decay(1.0f);
    p.set_damping(0.0f);
    const auto ir = render_ir(p, 1.5);

    const double early = window_energy(ir[0], 0.2, 0.4);
    const double late  = window_energy(ir[0], 1.2, 1.4);
    EXPECT_GT(late, early * 0.3);
}

TEST(Plate, PredelayShiftsOnset) {
    auto onset = [](float predelay_seconds) {
        dsp::plate p(1, 2, 2);
        p.set_predelay_seconds(predelay_seconds);
        const auto ir = render_ir(p, 0.5);
        for (size_t i = 0; i < ir[0].size(); ++i) {
            if (std::abs(ir[0][i]) > 1e-6f) {
                return i;
            }
        }
        return ir[0].size();
    };

    const size_t immediate = onset(0.0f);
    const size_t delayed   = onset(0.1f);
    const auto   shift     = static_cast<double>(delayed - immediate);
    EXPECT_NEAR(shift, 0.1 * k_fs, 0.005 * k_fs);
}

TEST(Plate, BlockSizeInvariant) {
    // The recurrence is strictly per-sample, so chunking must not matter.
    auto render_chunked = [](size_t chunk) {
        dsp::plate p(2, 3, 4);
        p.set_decay(0.75f);
        p.prepare(k_fs);
        p.snap_parameters();

        constexpr size_t total = 4096;
        // Deterministic two-channel noise (LCG).
        std::vector<std::vector<float>> in(2, std::vector<float>(total));
        std::uint32_t                   state = 22222u;
        for (auto& ch : in) {
            for (auto& s : ch) {
                state = state * 1664525u + 1013904223u;
                s     = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
            }
        }
        std::vector<std::vector<float>> out(3, std::vector<float>(total, 0.0f));

        for (size_t at = 0; at < total; at += chunk) {
            const size_t n       = std::min(chunk, total - at);
            const float* ins[2]  = {in[0].data() + at, in[1].data() + at};
            float*       outs[3] = {out[0].data() + at, out[1].data() + at, out[2].data() + at};
            p.process(ins, outs, n);
        }
        return out;
    };

    const auto by_64 = render_chunked(64);
    const auto by_17 = render_chunked(17);
    for (size_t ch = 0; ch < 3; ++ch) {
        for (size_t i = 0; i < by_64[ch].size(); ++i) {
            ASSERT_EQ(by_64[ch][i], by_17[ch][i]) << "channel " << ch << " sample " << i;
        }
    }
}

TEST(Plate, ResetClearsTail) {
    dsp::plate p(1, 2, 2);
    p.set_decay(0.9f);
    render_ir(p, 0.5); // leaves a ringing tank
    p.reset();

    std::vector<float> silence(k_block, 0.0f);
    std::vector<float> out0(k_block, 9.0f), out1(k_block, 9.0f);
    const float*       ins[1]  = {silence.data()};
    float*             outs[2] = {out0.data(), out1.data()};
    p.process(ins, outs, k_block);
    for (size_t i = 0; i < k_block; ++i) {
        EXPECT_EQ(out0[i], 0.0f);
        EXPECT_EQ(out1[i], 0.0f);
    }
}

TEST(Plate, ParameterRoundTrip) {
    dsp::plate p(1, 2, 2);
    p.set_decay(0.7f);
    p.set_damping(0.25f);
    p.set_bandwidth(0.5f);
    p.set_predelay_seconds(0.05f);
    p.set_mod_depth_ms(1.0f);
    p.set_mod_rate_hz(2.5f);
    EXPECT_FLOAT_EQ(p.decay(), 0.7f);
    EXPECT_FLOAT_EQ(p.damping(), 0.25f);
    EXPECT_FLOAT_EQ(p.bandwidth(), 0.5f);
    EXPECT_FLOAT_EQ(p.predelay_seconds(), 0.05f);
    EXPECT_FLOAT_EQ(p.mod_depth_ms(), 1.0f);
    EXPECT_FLOAT_EQ(p.mod_rate_hz(), 2.5f);

    // Out-of-range values clamp.
    p.set_decay(1.5f);
    p.set_predelay_seconds(9.0f);
    EXPECT_FLOAT_EQ(p.decay(), 1.0f);
    EXPECT_FLOAT_EQ(p.predelay_seconds(), dsp::plate::k_max_predelay_seconds);
}

#if AMBITAP_HAS_EXCEPTIONS
TEST(Plate, RejectsOutOfRangeCounts) {
    EXPECT_THROW(dsp::plate(0, 2, 2), std::invalid_argument);
    EXPECT_THROW(dsp::plate(2, 0, 2), std::invalid_argument);
    EXPECT_THROW(dsp::plate(2, 2, 1), std::invalid_argument);
    EXPECT_THROW(dsp::plate(dsp::k_plate_max_inputs + 1, 2, 2), std::invalid_argument);
    EXPECT_THROW(dsp::plate(2, dsp::k_plate_max_outputs + 1, 2), std::invalid_argument);
    EXPECT_THROW(dsp::plate(2, 2, dsp::k_plate_max_branches + 1), std::invalid_argument);
}
#endif

TEST(Plate, SettersDoNotRaceProcessing) {
    // TSan gate: hammer every RT-safe setter from a control thread while the
    // audio thread processes.
    dsp::plate p(2, 2, 4);
    p.prepare(48000.0f);

    std::atomic<bool> done{false};
    std::thread       control([&] {
        float v = 0.0f;
        while (!done.load(std::memory_order_acquire)) {
            v = v < 1.0f ? v + 0.01f : 0.0f;
            p.set_decay(v);
            p.set_damping(v * 0.5f);
            p.set_bandwidth(0.5f + v * 0.4f);
            p.set_input_diffusion_1(v * 0.9f);
            p.set_input_diffusion_2(v * 0.9f);
            p.set_decay_diffusion_1(v * 0.9f);
            p.set_decay_diffusion_2(v * 0.9f);
            p.set_predelay_seconds(v * 0.1f);
            p.set_mod_depth_ms(v);
            p.set_mod_rate_hz(v * 5.0f);
        }
    });

    std::vector<std::vector<float>> in(2, std::vector<float>(k_block, 0.1f));
    std::vector<std::vector<float>> out(2, std::vector<float>(k_block));
    const float*                    ins[2]  = {in[0].data(), in[1].data()};
    float*                          outs[2] = {out[0].data(), out[1].data()};
    for (int i = 0; i < 2000; ++i) {
        p.process(ins, outs, k_block);
    }
    done.store(true, std::memory_order_release);
    control.join();

    for (size_t i = 0; i < k_block; ++i) {
        EXPECT_TRUE(std::isfinite(out[0][i]));
        EXPECT_TRUE(std::isfinite(out[1][i]));
    }
}
