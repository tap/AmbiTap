/// AmbiTap: target-independent ambisonics library
/// C ABI implementation — see ambitap_capi.h.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#include "ambitap_capi.h"

#include <ambitap/ambitap.h>

#include <cstring>
#include <string>
#include <vector>

using namespace ambitap;

namespace {

    bool layout_by_name(const char* name, std::vector<spherical_coord>& out) {
        const std::string n = name ? name : "";
        if (n == "stereo")
            out = layouts::stereo();
        else if (n == "quad")
            out = layouts::quad();
        else if (n == "5.1")
            out = layouts::surround_5_1();
        else if (n == "hexagon")
            out = layouts::hexagon();
        else if (n == "7.1")
            out = layouts::surround_7_1();
        else if (n == "cube")
            out = layouts::cube();
        else if (n == "octagon")
            out = layouts::octagon();
        else if (n == "7.1.4")
            out = layouts::surround_7_1_4();
        else
            return false;
        return true;
    }

} // namespace

extern "C" {

int ambitap_channel_count(int order) {
    if (order < 0 || order > max_order) return -1;
    return static_cast<int>(channel_count(order));
}

int ambitap_evaluate_sh(int order, float azimuth, float elevation, float* out) {
    if (!out || order < 0 || order > max_order) return -1;
    evaluate_sh(order, azimuth, elevation, out);
    return 0;
}

int ambitap_max_re_weights(int order, int energy_normalized, float* out) {
    if (!out || order < 0 || order > max_order) return -1;
    const auto w =
        energy_normalized ? max_re_weights_energy_normalized(order) : max_re_weights(order);
    std::memcpy(out, w.data(), w.size() * sizeof(float));
    return 0;
}

int ambitap_sh_rotation_matrix(int order, float yaw, float pitch, float roll, float* out) {
    if (!out || order < 0 || order > max_order) return -1;
    try {
        sh_rotation rot(order, yaw, pitch, roll);
        const auto& m = rot.matrix();
        const auto  C = static_cast<Eigen::Index>(channel_count(order));
        for (Eigen::Index r = 0; r < C; ++r) {
            for (Eigen::Index c = 0; c < C; ++c) {
                out[static_cast<size_t>(r * C + c)] = m(r, c);
            }
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_layout_preset(const char* name, float* az, float* el, int cap) {
    std::vector<spherical_coord> speakers;
    if (!az || !el || !layout_by_name(name, speakers)) return -1;
    if (static_cast<size_t>(cap) < speakers.size()) return -1;
    for (size_t i = 0; i < speakers.size(); ++i) {
        az[i] = speakers[i].azimuth;
        el[i] = speakers[i].elevation;
    }
    return static_cast<int>(speakers.size());
}

int ambitap_vbap_gains(int n_speakers, const float* az, const float* el, float src_azimuth,
                       float src_elevation, float* out) {
    if (!az || !el || !out || n_speakers <= 0) return -1;
    try {
        std::vector<spherical_coord> speakers;
        speakers.reserve(static_cast<size_t>(n_speakers));
        for (int i = 0; i < n_speakers; ++i) speakers.push_back({az[i], el[i]});
        speaker_layout layout(speakers);
        const auto     gains = layout.vbap_gains({src_azimuth, src_elevation});
        std::memcpy(out, gains.data(), gains.size() * sizeof(float));
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_decoder_matrix(const char* algorithm, int order, int n_speakers, const float* az,
                           const float* el, int use_max_re, float* out) {
    if (!algorithm || !az || !el || !out || n_speakers <= 0) return -1;
    try {
        std::vector<spherical_coord> speakers;
        speakers.reserve(static_cast<size_t>(n_speakers));
        for (int i = 0; i < n_speakers; ++i) speakers.push_back({az[i], el[i]});

        const std::string alg = algorithm;
        Eigen::MatrixXf   D;
        if (alg == "mode_match")
            D = compute_mode_matching_decoder(order, speakers, use_max_re);
        else if (alg == "allrad")
            D = compute_allrad_decoder(order, speakers, use_max_re);
        else if (alg == "epad")
            D = compute_epad_decoder(order, speakers, use_max_re);
        else
            return -1;

        for (Eigen::Index r = 0; r < D.rows(); ++r) {
            for (Eigen::Index c = 0; c < D.cols(); ++c) {
                out[static_cast<size_t>(r * D.cols() + c)] = D(r, c);
            }
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_builtin_hrtf_info(int* order, int* channels, int* length, float* sample_rate) {
    if (order) *order = builtin_hrtf_order;
    if (channels) *channels = static_cast<int>(builtin_hrtf_channels);
    if (length) *length = static_cast<int>(builtin_hrtf_length);
    if (sample_rate) *sample_rate = builtin_hrtf_sample_rate;
    return 0;
}

int ambitap_builtin_hrtf_fir(int magls, int ear, int channel, float* out) {
    if (!out || ear < 0 || ear > 1 || channel < 0
        || channel >= static_cast<int>(builtin_hrtf_channels)) {
        return -1;
    }
    const float(*table)[builtin_hrtf_length] =
        magls ? (ear == 0 ? builtin_hrtf_magls_left : builtin_hrtf_magls_right)
              : (ear == 0 ? builtin_hrtf_left : builtin_hrtf_right);
    std::memcpy(out, table[channel], builtin_hrtf_length * sizeof(float));
    return 0;
}

int ambitap_resample_fir(const float* in, int in_len, float in_rate, float out_rate, float* out,
                         int out_cap) {
    if (!in || !out || in_len <= 0 || in_rate <= 0.f || out_rate <= 0.f) return -1;
    try {
        const auto resampled = resample_fir(in, static_cast<size_t>(in_len), in_rate, out_rate);
        if (resampled.size() > static_cast<size_t>(out_cap)) return -1;
        std::memcpy(out, resampled.data(), resampled.size() * sizeof(float));
        return static_cast<int>(resampled.size());
    }
    catch (...) {
        return -1;
    }
}

int ambitap_convolve(const float* x, int n, const float* ir, int ir_len, int block_size,
                     float* out) {
    if (!x || !ir || !out || n <= 0 || ir_len <= 0 || block_size < 4) return -1;
    try {
        const auto            block = static_cast<size_t>(block_size);
        partitioned_convolver conv(block, ir, static_cast<size_t>(ir_len));
        std::vector<float>    in_block(block, 0.f), out_block(block, 0.f);
        for (int start = 0; start < n; start += block_size) {
            const auto len = static_cast<size_t>(std::min(block_size, n - start));
            std::memcpy(in_block.data(), x + start, len * sizeof(float));
            std::fill(in_block.begin() + static_cast<long>(len), in_block.end(), 0.f);
            conv.process(in_block.data(), out_block.data());
            std::memcpy(out + start, out_block.data(), len * sizeof(float));
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_binaural_render(int order, float sample_rate, int magls, const float* mono,
                            int n_frames, const float* az, const float* el, float head_yaw,
                            float head_pitch, float head_roll, float* left, float* right) {
    if (!mono || !az || !el || !left || !right || n_frames <= 0) return -1;
    try {
        constexpr size_t block = 64;

        dsp::encoder enc(order);
        enc.set_direction(az[0], el[0]);
        enc.snap_parameters();

        dsp::binaural_renderer bin(order);
        if (magls) bin.set_projection(dsp::binaural_renderer::hrtf_projection::magls);
        bin.prepare(block, sample_rate);
        if (head_yaw != 0.f || head_pitch != 0.f || head_roll != 0.f) {
            bin.set_head_orientation(head_yaw, head_pitch, head_roll);
            bin.wait_for_settling();
        }

        const size_t                    channels = enc.channels();
        std::vector<std::vector<float>> hoa(channels, std::vector<float>(block));
        std::vector<float*>             hoa_out;
        std::vector<const float*>       hoa_in;
        for (auto& b : hoa) {
            hoa_out.push_back(b.data());
            hoa_in.push_back(b.data());
        }
        std::vector<float> mono_block(block), l_block(block), r_block(block);

        for (int start = 0; start < n_frames; start += static_cast<int>(block)) {
            const auto len =
                static_cast<size_t>(std::min(static_cast<int>(block), n_frames - start));
            std::memcpy(mono_block.data(), mono + start, len * sizeof(float));
            std::fill(mono_block.begin() + static_cast<long>(len), mono_block.end(), 0.f);

            // Direction sampled at the block head; the encoder's parameter
            // ramp interpolates within the block (click-free motion).
            enc.set_direction(az[start], el[start]);
            enc.process(mono_block.data(), hoa_out.data(), block);
            bin.process(hoa_in.data(), l_block.data(), r_block.data(), block);

            std::memcpy(left + start, l_block.data(), len * sizeof(float));
            std::memcpy(right + start, r_block.data(), len * sizeof(float));
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

} // extern "C"
