/// AmbiTap example: render a moving HOA source to binaural stereo with the
/// built-in KEMAR HRTF set and write the result as a 16-bit WAV file.
///
/// Build:  cmake -B build && cmake --build build --target binaural_render
/// Run:    ./build/examples/binaural_render [out.wav]
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <ambitap/ambitap.h>

namespace {

    // Minimal 16-bit stereo WAV writer.
    void write_wav(const char* path, const std::vector<float>& left, const std::vector<float>& right,
                   std::uint32_t sample_rate) {
        const std::uint32_t frames      = static_cast<std::uint32_t>(left.size());
        const std::uint32_t data_bytes  = frames * 2u * 2u;
        const std::uint32_t riff_bytes  = 36u + data_bytes;
        const std::uint16_t channels    = 2;
        const std::uint16_t bits        = 16;
        const std::uint32_t byte_rate   = sample_rate * channels * (bits / 8u);
        const std::uint16_t block_align = channels * (bits / 8u);

        std::ofstream f(path, std::ios::binary);
        auto          u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        auto          u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };

        f.write("RIFF", 4);
        u32(riff_bytes);
        f.write("WAVE", 4);
        f.write("fmt ", 4);
        u32(16);
        u16(1); // PCM
        u16(channels);
        u32(sample_rate);
        u32(byte_rate);
        u16(block_align);
        u16(bits);
        f.write("data", 4);
        u32(data_bytes);
        for (std::uint32_t i = 0; i < frames; ++i) {
            for (float v : {left[i], right[i]}) {
                const float clamped = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
                const auto  s       = static_cast<std::int16_t>(clamped * 32767.f);
                f.write(reinterpret_cast<const char*>(&s), 2);
            }
        }
    }

} // namespace

int main(int argc, char** argv) {
    const char* out_path = (argc > 1) ? argv[1] : "binaural_render.wav";

    constexpr int    order = 3;
    constexpr size_t block = 64;
    constexpr float  pi    = 3.14159265358979f;
    // NOTE: the built-in HRTF set is sampled at 44.1 kHz; pass the host rate
    // to prepare() and the renderer resamples the FIRs to match.
    constexpr float  sample_rate = 48000.0f;
    constexpr size_t seconds     = 4;
    constexpr size_t frames      = static_cast<size_t>(sample_rate) * seconds;

    tap::ambi::dsp::encoder enc(order);
    enc.snap_parameters();

    tap::ambi::dsp::binaural_renderer bin(order);
    bin.prepare(block, sample_rate);

    std::vector<float> left(frames), right(frames);

    std::vector<std::vector<float>> hoa(enc.channels(), std::vector<float>(block));
    std::vector<float*>             hoa_out;
    std::vector<const float*>       hoa_in;
    for (auto& b : hoa) {
        hoa_out.push_back(b.data());
        hoa_in.push_back(b.data());
    }
    std::vector<float> mono(block);

    // A 330 Hz tone orbiting the head once per two seconds.
    size_t n = 0;
    for (size_t start = 0; start + block <= frames; start += block) {
        const float az = 2.0f * pi * static_cast<float>(start) / (2.0f * sample_rate); // one revolution / 2 s
        enc.set_direction(az, 0.0f); // ramped by the encoder: click-free motion
        for (size_t i = 0; i < block; ++i, ++n) {
            mono[i] = 0.5f * std::sin(2.0f * pi * 330.0f * static_cast<float>(n) / sample_rate);
        }
        enc.process(mono.data(), hoa_out.data(), block);
        bin.process(hoa_in.data(), left.data() + start, right.data() + start, block);
    }

    write_wav(out_path, left, right, static_cast<std::uint32_t>(sample_rate));
    std::printf("wrote %zu s of orbiting binaural audio to %s\n", seconds, out_path);
    return 0;
}
