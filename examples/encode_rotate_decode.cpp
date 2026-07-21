/// AmbiTap example: encode a mono source into HOA, rotate the soundfield,
/// and decode it to a 5.1 loudspeaker layout — the minimal end-to-end
/// signal path, run offline.
///
/// Build:  cmake -B build && cmake --build build --target encode_rotate_decode
/// Run:    ./build/examples/encode_rotate_decode
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <ambitap/ambitap.h>

int main() {
    constexpr int    order  = 3;
    constexpr size_t frames = 4800; // 100 ms at 48 kHz
    constexpr float  k_pi   = 3.14159265358979f;

    // 1. Encode a 440 Hz tone as a point source 45° to the left.
    tap::ambi::dsp::encoder enc(order);
    enc.set_direction(45.0f * k_pi / 180.0f, 0.0f);
    enc.snap_parameters(); // offline render: no parameter ramp

    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = std::sin(2.0f * k_pi * 440.0f * static_cast<float>(i) / 48000.0f);
    }
    std::vector<std::vector<float>> hoa(enc.channels(), std::vector<float>(frames));
    std::vector<float*>             hoa_ptrs;
    for (auto& b : hoa) {
        hoa_ptrs.push_back(b.data());
    }
    enc.process(mono.data(), hoa_ptrs.data(), frames);

    // 2. Rotate the whole soundfield 90° to the right (yaw = -90°).
    tap::ambi::dsp::rotator rot(order);
    rot.set_rotation(-90.0f * k_pi / 180.0f, 0.0f, 0.0f);
    rot.wait_for_settling(); // offline: block until the matrix is built

    std::vector<std::vector<float>> rotated(rot.channels(), std::vector<float>(frames));
    std::vector<const float*>       rot_in;
    std::vector<float*>             rot_out;
    for (size_t ch = 0; ch < rot.channels(); ++ch) {
        rot_in.push_back(hoa[ch].data());
        rot_out.push_back(rotated[ch].data());
    }
    rot.process(rot_in.data(), rot_out.data(), frames);

    // 3. Decode to 5.1 (2D pairwise VBAP under the hood via ALLRAD).
    const auto              speakers = tap::ambi::layouts::surround_5_1();
    tap::ambi::dsp::decoder dec(order);
    dec.set_algorithm(tap::ambi::dsp::decoder_algorithm::allrad);
    dec.set_speakers(speakers);
    dec.wait_for_settling();

    std::vector<std::vector<float>> out(speakers.size(), std::vector<float>(frames));
    std::vector<const float*>       dec_in;
    std::vector<float*>             dec_out;
    for (size_t ch = 0; ch < rot.channels(); ++ch) {
        dec_in.push_back(rotated[ch].data());
    }
    for (auto& b : out) {
        dec_out.push_back(b.data());
    }
    dec.process(dec_in.data(), dec_out.data(), speakers.size(), frames);

    // Report per-speaker RMS: the source started 45° left, the scene turned
    // 90° right, so it should now sit 45° right — loudest between C and R.
    static const char* names[] = {"L ", "R ", "C ", "LS", "RS"};
    std::printf("per-speaker RMS after encode(+45°) -> rotate(-90°) -> decode(5.1):\n");
    for (size_t s = 0; s < speakers.size(); ++s) {
        double e = 0.0;
        // Skip the adoption crossfade at the head of the buffer.
        const size_t start = tap::ambi::dsp::decoder::k_fade_samples;
        for (size_t i = start; i < frames; ++i) {
            e += static_cast<double>(out[s][i]) * static_cast<double>(out[s][i]);
        }
        const double rms = std::sqrt(e / static_cast<double>(frames - start));
        std::printf("  %s  %.4f  %s\n", names[s], rms, std::string(static_cast<size_t>(rms * 60.0), '#').c_str());
    }
    return 0;
}
