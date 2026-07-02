/// AmbiTap: target-independent ambisonics library
/// Concurrency stress tests: control-thread setters hammering against a live
/// audio thread, and construction/destruction races. Run these under TSan
/// (CI has a dedicated leg); they also catch use-after-free under ASan.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/analysis/soundfield_grid.h"
#include "ambitap/dsp/decoder.h"
#include "ambitap/dsp/encoder.h"
#include "ambitap/dsp/rotator.h"
#include "ambitap/dsp/util/rt_published.h"
#include "ambitap/math/geometry/layouts.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

using namespace ambitap;

namespace {

// Duration knobs: large enough to interleave thousands of publishes with a
// live audio loop, small enough to keep the suite fast.
constexpr int k_setter_iterations = 2000;

bool all_finite(const std::vector<float>& v) {
    for (float x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}

} // namespace

// rt_published core property: the reader never observes a torn or freed
// product. Each product is a vector filled with one constant; any mixed or
// garbage values mean a torn read or use-after-free.
TEST(RtPublished, ReaderNeverSeesTornOrFreedProduct) {
    dsp::rt_published<const std::vector<float>> pub;

    std::atomic<bool> stop {false};
    std::atomic<int>  inconsistencies {0};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            auto        guard = pub.read_lock();
            const auto* p     = guard.get();
            if (p) {
                const float first = (*p)[0];
                for (float v : *p) {
                    if (v != first) inconsistencies.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    for (int i = 0; i < k_setter_iterations; ++i) {
        pub.publish(std::make_shared<std::vector<float>>(512, static_cast<float>(i)));
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    EXPECT_EQ(inconsistencies.load(), 0);
    ASSERT_NE(pub.peek(), nullptr);
    EXPECT_EQ((*pub.peek())[0], static_cast<float>(k_setter_iterations - 1));
}

// Control thread hammers set_rotation while the audio thread processes
// continuously: no crash, no race (TSan), always-finite output.
TEST(DspThreads, RotatorSetterHammerVsProcess) {
    constexpr int order = 3;
    dsp::rotator  rot(order);

    std::vector<float> in(rot.channels(), 0.5f), out(rot.channels());
    std::atomic<bool>  stop {false};
    std::atomic<bool>  bad {false};

    std::thread audio([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            rot.process_frame(in.data(), out.data());
            if (!all_finite(out)) bad.store(true, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < k_setter_iterations; ++i) {
        rot.set_rotation(static_cast<float>(i) * 0.01f, 0.2f, -0.1f);
    }
    rot.wait_for_settling();
    stop.store(true, std::memory_order_relaxed);
    audio.join();

    EXPECT_FALSE(bad.load());
    EXPECT_TRUE(rot.is_active());
}

// Speaker-layout and algorithm changes racing a live decode, including
// dimension changes (cube <-> 5.1) which switch the output width fade path.
TEST(DspThreads, DecoderSpeakerHammerVsProcess) {
    constexpr int order = 3;
    dsp::decoder  dec(order);
    dec.set_speakers(layouts::cube());
    dec.wait_for_settling();

    std::vector<float> in(dec.input_channels(), 0.25f);
    std::vector<float> out(8);
    std::atomic<bool>  stop {false};
    std::atomic<bool>  bad {false};

    std::thread audio([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            dec.process_frame(in.data(), out.data(), 8);
            if (!all_finite(out)) bad.store(true, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < k_setter_iterations; ++i) {
        switch (i % 3) {
            case 0: dec.set_speakers(layouts::cube()); break;
            case 1: dec.set_speakers(layouts::octagon()); break;
            case 2:
                dec.set_algorithm((i % 2) ? dsp::decoder_algorithm::allrad
                                          : dsp::decoder_algorithm::mode_match);
                break;
        }
    }
    dec.wait_for_settling();
    stop.store(true, std::memory_order_relaxed);
    audio.join();

    EXPECT_FALSE(bad.load());
}

// Direction/gain hammering against the smoothed-coefficient audio path.
TEST(DspThreads, EncoderDirectionHammerVsProcess) {
    dsp::encoder enc(5);

    std::vector<std::vector<float>> bufs(enc.channels(), std::vector<float>(16));
    std::vector<float*>             out;
    for (auto& b : bufs) out.push_back(b.data());
    std::vector<float> mono(16, 1.f);

    std::atomic<bool> stop {false};
    std::atomic<bool> bad {false};

    std::thread audio([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            enc.process(mono.data(), out.data(), 16);
            for (auto& b : bufs) {
                if (!all_finite(b)) bad.store(true, std::memory_order_relaxed);
            }
        }
    });

    for (int i = 0; i < k_setter_iterations; ++i) {
        enc.set_direction(static_cast<float>(i) * 0.013f, static_cast<float>(i % 90) * 0.01f);
        enc.set_gain(0.5f + static_cast<float>(i % 10) * 0.1f);
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();

    EXPECT_FALSE(bad.load());
}

// Grid resizes (control thread) racing the covariance accumulation (audio).
TEST(DspThreads, SoundfieldGridResizeVsProcess) {
    analysis::soundfield_grid grid(2, 16);
    grid.prepare(48000.f);

    std::vector<std::vector<float>> bufs(grid.channels(), std::vector<float>(32, 0.1f));
    std::vector<const float*>       in;
    for (auto& b : bufs) in.push_back(b.data());

    std::atomic<bool> stop {false};

    std::thread audio([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            grid.process(in.data(), 32);
        }
    });

    for (int i = 0; i < 500; ++i) {
        grid.set_azimuth_steps((i % 2) ? 16 : 32);
        auto img = grid.snapshot(60.f); // concurrent UI read
        (void)img;
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();

    EXPECT_GT(grid.azimuth_steps(), 0);
}

// Destroying a processor immediately after submitting work must join the
// worker cleanly — no leak (ASan), no use-after-free, no deadlock.
TEST(DspThreads, DestroyWhileBuilding) {
    for (int i = 0; i < 50; ++i) {
        dsp::decoder dec(5);
        dec.set_speakers(layouts::surround_7_1_4());
        // no wait: destructor races the in-flight build
    }
    for (int i = 0; i < 50; ++i) {
        dsp::rotator rot(5);
        rot.set_rotation(0.3f, 0.2f, 0.1f);
    }
    SUCCEED();
}
