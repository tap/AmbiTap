/// AmbiTap benchmarks: per-block wall-time cost of the real-time process()
/// paths across orders, reported as microseconds per 64-frame block and as
/// percent of the real-time budget at 48 kHz (64 frames = 1333 us).
///
/// Dependency-free by design (std::chrono; median of repeated runs) so it
/// builds everywhere the library does. Build with -DAMBITAP_BUILD_BENCH=ON.
/// Copyright 2026 Timothy Place. MIT License.

#include <ambitap/ambitap.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

    constexpr size_t k_block = 64;
    constexpr float  k_fs    = 48000.0f;
    constexpr int    k_reps  = 400; // blocks per timed run
    constexpr int    k_runs  = 9;   // runs; median reported

    using clock_t_ = std::chrono::steady_clock;

    float g_sink = 0.f; // defeat dead-code elimination

    struct planar {
        std::vector<std::vector<float>> bufs;
        std::vector<const float*>       in;
        std::vector<float*>             out;
        planar(size_t channels, size_t frames, float fill)
            : bufs(channels, std::vector<float>(frames, fill)) {
            for (auto& b : bufs) {
                in.push_back(b.data());
                out.push_back(b.data());
            }
        }
    };

    template <typename Fn> double bench_us_per_block(Fn&& fn) {
        std::vector<double> runs;
        for (int r = 0; r < k_runs; ++r) {
            const auto t0 = clock_t_::now();
            for (int i = 0; i < k_reps; ++i) fn();
            const auto t1 = clock_t_::now();
            runs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count()
                           / static_cast<double>(k_reps));
        }
        std::sort(runs.begin(), runs.end());
        return runs[runs.size() / 2];
    }

    void report(const std::string& name, double us) {
        const double budget_us = 1e6 * static_cast<double>(k_block) / static_cast<double>(k_fs);
        std::printf("%-28s %9.2f us/block   %6.2f %% of RT budget\n", name.c_str(), us,
                    100.0 * us / budget_us);
    }

} // namespace

int main() {
    std::printf("AmbiTap process() benchmarks — %zu-frame blocks, %.0f kHz "
                "(budget %.0f us/block), median of %d runs x %d blocks\n\n",
                k_block, k_fs / 1000.f, 1e6 * k_block / k_fs, k_runs, k_reps);

    for (int order : {1, 3, 5}) {
        const size_t       channels = ambitap::channel_count(order);
        planar             hoa(channels, k_block, 0.25f);
        std::vector<float> mono(k_block, 0.5f);

        {
            ambitap::dsp::encoder enc(order);
            enc.set_direction(0.4f, 0.1f);
            enc.snap_parameters();
            report("encoder      order " + std::to_string(order), bench_us_per_block([&] {
                       enc.process(mono.data(), hoa.out.data(), k_block);
                       g_sink += hoa.bufs[0][0];
                   }));
        }
        {
            ambitap::dsp::rotator rot(order);
            rot.set_rotation(0.4f, 0.2f, -0.1f);
            rot.wait_for_settling();
            planar dst(channels, k_block, 0.f);
            // run out the adoption crossfade so the settled path is measured
            for (size_t i = 0; i < ambitap::dsp::rotator::k_fade_samples / k_block + 1; ++i) {
                rot.process(hoa.in.data(), dst.out.data(), k_block);
            }
            report("rotator      order " + std::to_string(order), bench_us_per_block([&] {
                       rot.process(hoa.in.data(), dst.out.data(), k_block);
                       g_sink += dst.bufs[0][0];
                   }));
        }
        {
            ambitap::dsp::decoder dec(order);
            dec.set_speakers(ambitap::layouts::cube());
            dec.wait_for_settling();
            planar spk(8, k_block, 0.f);
            for (size_t i = 0; i < ambitap::dsp::decoder::k_fade_samples / k_block + 1; ++i) {
                dec.process(hoa.in.data(), spk.out.data(), 8, k_block);
            }
            report("decoder/cube order " + std::to_string(order), bench_us_per_block([&] {
                       dec.process(hoa.in.data(), spk.out.data(), 8, k_block);
                       g_sink += spk.bufs[0][0];
                   }));
        }
        {
            ambitap::dsp::binaural_renderer bin(order);
            bin.prepare(k_block, k_fs);
            std::vector<float> l(k_block), r(k_block);
            report("binaural     order " + std::to_string(order), bench_us_per_block([&] {
                       bin.process(hoa.in.data(), l.data(), r.data(), k_block);
                       g_sink += l[0];
                   }));
        }
        std::printf("\n");
    }

    // Rebuild latency: how long a decoder matrix build takes off the audio
    // thread (order 5, 7.1.4 via ALLRAD — the expensive path).
    {
        ambitap::dsp::decoder dec(5);
        dec.set_algorithm(ambitap::dsp::decoder_algorithm::allrad);
        const auto t0 = clock_t_::now();
        dec.set_speakers(ambitap::layouts::surround_7_1_4());
        dec.wait_for_settling();
        const auto t1 = clock_t_::now();
        std::printf("decoder rebuild (allrad, order 5, 7.1.4): %.1f ms (worker thread)\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    return g_sink == 12345.f ? 1 : 0;
}
