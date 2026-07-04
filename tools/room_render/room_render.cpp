/// AmbiTap: target-independent ambisonics library
/// Offline renderer for dsp::room — writes the SH-domain impulse response of
/// a stated configuration as raw float32 (channels-major), latency-trimmed,
/// so notebooks/room_verification.ipynb's R1-R10 gate functions can evaluate
/// the C++ implementation exactly as they evaluated the offline prototype.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap/dsp/room.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

    void usage(const char* argv0) {
        std::fprintf(stderr,
                     "usage: %s --out FILE [--mode full|er|tail|direct] [--source X Y Z]\n"
                     "          [--seconds S] [--sr HZ] [--block N] [--order N]\n"
                     "Renders the dsp::room SH impulse response (seed-11 configuration by\n"
                     "default) as raw little-endian float32, shape (channels, samples),\n"
                     "channel-major, with the object's fixed latency trimmed off.\n",
                     argv0);
    }

} // namespace

int main(int argc, char** argv) {
    std::string out_path;
    std::string mode        = "full";
    double      seconds     = 2.0;
    double      sr          = 48000.0;
    size_t      block       = 256;
    int         order       = 3;
    bool        have_source = false;
    float       src[3]      = {0.0f, 0.0f, 0.0f};

    for (int i = 1; i < argc; ++i) {
        const std::string arg  = argv[i];
        auto              next = [&](double& v) {
            if (i + 1 >= argc) return false;
            v = std::atof(argv[++i]);
            return true;
        };
        if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        }
        else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        }
        else if (arg == "--source" && i + 3 < argc) {
            src[0]      = static_cast<float>(std::atof(argv[++i]));
            src[1]      = static_cast<float>(std::atof(argv[++i]));
            src[2]      = static_cast<float>(std::atof(argv[++i]));
            have_source = true;
        }
        else if (arg == "--seconds") {
            if (!next(seconds)) break;
        }
        else if (arg == "--sr") {
            if (!next(sr)) break;
        }
        else if (arg == "--block") {
            double v = 0;
            if (!next(v)) break;
            block = static_cast<size_t>(v);
        }
        else if (arg == "--order") {
            double v = 0;
            if (!next(v)) break;
            order = static_cast<int>(v);
        }
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (out_path.empty()) {
        usage(argv[0]);
        return 2;
    }

    ambitap::dsp::room room(order);
    if (have_source) room.set_source_position(src[0], src[1], src[2]);
    if (mode == "er") {
        room.set_tail_enabled(false);
    }
    else if (mode == "tail") {
        room.set_direct_enabled(false);
        room.set_early_enabled(false);
    }
    else if (mode == "direct") {
        room.set_early_enabled(false);
        room.set_tail_enabled(false);
    }
    else if (mode != "full") {
        usage(argv[0]);
        return 2;
    }
    room.prepare(block, static_cast<float>(sr));
    room.wait_for_settling();
    room.snap_parameters();

    const size_t channels  = room.channels();
    const size_t latency   = room.latency_samples();
    const size_t n_samples = static_cast<size_t>(seconds * sr + 0.5);
    const size_t total     = latency + n_samples;
    const size_t blocks    = (total + block - 1) / block;

    std::vector<float>              input(block, 0.0f);
    std::vector<std::vector<float>> out_bufs(channels, std::vector<float>(block, 0.0f));
    std::vector<float*>             out_ptrs;
    for (auto& b : out_bufs) out_ptrs.push_back(b.data());
    std::vector<std::vector<float>> ir(channels, std::vector<float>(blocks * block, 0.0f));

    for (size_t bi = 0; bi < blocks; ++bi) {
        std::fill(input.begin(), input.end(), 0.0f);
        if (bi == 0) input[0] = 1.0f; // unit impulse at t = 0
        room.process(input.data(), out_ptrs.data(), block);
        for (size_t ch = 0; ch < channels; ++ch) {
            std::memcpy(ir[ch].data() + bi * block, out_bufs[ch].data(), block * sizeof(float));
        }
    }

    std::ofstream f(out_path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", out_path.c_str());
        return 1;
    }
    for (size_t ch = 0; ch < channels; ++ch) {
        f.write(reinterpret_cast<const char*>(ir[ch].data() + latency),
                static_cast<std::streamsize>(n_samples * sizeof(float)));
    }
    f.close();
    if (!f) {
        std::fprintf(stderr, "short write to %s\n", out_path.c_str());
        return 1;
    }

    std::printf("wrote %s: order %d (%zu ch) x %zu samples at %.0f Hz, mode %s, "
                "latency trimmed %zu\n",
                out_path.c_str(), room.order(), channels, n_samples, sr, mode.c_str(), latency);
    return 0;
}
