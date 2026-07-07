/// @file ambitap_capi.cpp
/// @brief C ABI implementation — see ambitap_capi.h.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include "ambitap_capi.h"

#include <cstring>
#include <string>
#include <vector>

#include <ambitap/ambitap.h>

using namespace ambitap;

namespace {

    bool algorithm_by_name(const char* name, dsp::decoder_algorithm& out) {
        const std::string n = name ? name : "";
        if (n == "mode_match") {
            out = dsp::decoder_algorithm::mode_match;
        }
        else if (n == "allrad") {
            out = dsp::decoder_algorithm::allrad;
        }
        else if (n == "epad") {
            out = dsp::decoder_algorithm::epad;
        }
        else {
            return false;
        }
        return true;
    }

    bool layout_by_name(const char* name, std::vector<spherical_coord>& out) {
        const std::string n = name ? name : "";
        if (n == "stereo") {
            out = layouts::stereo();
        }
        else if (n == "quad") {
            out = layouts::quad();
        }
        else if (n == "5.1") {
            out = layouts::surround_5_1();
        }
        else if (n == "hexagon") {
            out = layouts::hexagon();
        }
        else if (n == "7.1") {
            out = layouts::surround_7_1();
        }
        else if (n == "cube") {
            out = layouts::cube();
        }
        else if (n == "octagon") {
            out = layouts::octagon();
        }
        else if (n == "7.1.4") {
            out = layouts::surround_7_1_4();
        }
        else {
            return false;
        }
        return true;
    }

} // namespace

extern "C" {

int ambitap_channel_count(int order) {
    if (order < 0 || order > k_max_order) {
        return -1;
    }
    return static_cast<int>(channel_count(order));
}

int ambitap_evaluate_sh(int order, float azimuth, float elevation, float* out) {
    if (!out || order < 0 || order > k_max_order) {
        return -1;
    }
    evaluate_sh(order, azimuth, elevation, out);
    return 0;
}

int ambitap_max_re_weights(int order, int energy_normalized, float* out) {
    if (!out || order < 0 || order > k_max_order) {
        return -1;
    }
    const auto w = energy_normalized ? max_re_weights_energy_normalized(order) : max_re_weights(order);
    std::memcpy(out, w.data(), w.size() * sizeof(float));
    return 0;
}

int ambitap_sh_rotation_matrix(int order, float yaw, float pitch, float roll, float* out) {
    if (!out || order < 0 || order > k_max_order) {
        return -1;
    }
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
    if (!az || !el || !layout_by_name(name, speakers)) {
        return -1;
    }
    if (static_cast<size_t>(cap) < speakers.size()) {
        return -1;
    }
    for (size_t i = 0; i < speakers.size(); ++i) {
        az[i] = speakers[i].azimuth;
        el[i] = speakers[i].elevation;
    }
    return static_cast<int>(speakers.size());
}

int ambitap_vbap_gains(int n_speakers, const float* az, const float* el, float src_azimuth, float src_elevation,
                       float* out) {
    if (!az || !el || !out || n_speakers <= 0) {
        return -1;
    }
    try {
        std::vector<spherical_coord> speakers;
        speakers.reserve(static_cast<size_t>(n_speakers));
        for (int i = 0; i < n_speakers; ++i) {
            speakers.push_back({az[i], el[i]});
        }
        speaker_layout layout(speakers);
        const auto     gains = layout.vbap_gains({src_azimuth, src_elevation});
        std::memcpy(out, gains.data(), gains.size() * sizeof(float));
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_decoder_matrix(const char* algorithm, int order, int n_speakers, const float* az, const float* el,
                           int use_max_re, float* out) {
    if (!algorithm || !az || !el || !out || n_speakers <= 0) {
        return -1;
    }
    try {
        std::vector<spherical_coord> speakers;
        speakers.reserve(static_cast<size_t>(n_speakers));
        for (int i = 0; i < n_speakers; ++i) {
            speakers.push_back({az[i], el[i]});
        }

        const std::string alg = algorithm;
        Eigen::MatrixXf   D;
        if (alg == "mode_match") {
            D = compute_mode_matching_decoder(order, speakers, use_max_re);
        }
        else if (alg == "allrad") {
            D = compute_allrad_decoder(order, speakers, use_max_re);
        }
        else if (alg == "epad") {
            D = compute_epad_decoder(order, speakers, use_max_re);
        }
        else {
            return -1;
        }

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
    if (order) {
        *order = builtin_hrtf_order;
    }
    if (channels) {
        *channels = static_cast<int>(builtin_hrtf_channels);
    }
    if (length) {
        *length = static_cast<int>(builtin_hrtf_length);
    }
    if (sample_rate) {
        *sample_rate = builtin_hrtf_sample_rate;
    }
    return 0;
}

int ambitap_builtin_hrtf_fir(int magls, int ear, int channel, float* out) {
    if (!out || ear < 0 || ear > 1 || channel < 0 || channel >= static_cast<int>(builtin_hrtf_channels)) {
        return -1;
    }
    const float(*table)[builtin_hrtf_length] = magls ? (ear == 0 ? builtin_hrtf_magls_left : builtin_hrtf_magls_right)
                                                     : (ear == 0 ? builtin_hrtf_left : builtin_hrtf_right);
    std::memcpy(out, table[channel], builtin_hrtf_length * sizeof(float));
    return 0;
}

int ambitap_builtin_hrtf_hrir(int order, int magls, float azimuth, float elevation, float* left, float* right) {
    if (!left || !right || order < 1 || order > builtin_hrtf_order) {
        return -1;
    }
    float sh[k_max_channel_count];
    evaluate_sh(order, azimuth, elevation, sh);

    float* const ears[2] = {left, right};
    for (int ear = 0; ear < 2; ++ear) {
        const float(*table)[builtin_hrtf_length] = magls
                                                       ? (ear == 0 ? builtin_hrtf_magls_left : builtin_hrtf_magls_right)
                                                       : (ear == 0 ? builtin_hrtf_left : builtin_hrtf_right);
        std::fill(ears[ear], ears[ear] + builtin_hrtf_length, 0.f);
        for (size_t ch = 0; ch < channel_count(order); ++ch) {
            for (size_t t = 0; t < builtin_hrtf_length; ++t) {
                ears[ear][t] += sh[ch] * table[ch][t];
            }
        }
    }
    return 0;
}

int ambitap_resample_fir(const float* in, int in_len, float in_rate, float out_rate, float* out, int out_cap) {
    if (!in || !out || in_len <= 0 || in_rate <= 0.f || out_rate <= 0.f) {
        return -1;
    }
    try {
        const auto resampled = resample_fir(in, static_cast<size_t>(in_len), in_rate, out_rate);
        if (resampled.size() > static_cast<size_t>(out_cap)) {
            return -1;
        }
        std::memcpy(out, resampled.data(), resampled.size() * sizeof(float));
        return static_cast<int>(resampled.size());
    }
    catch (...) {
        return -1;
    }
}

int ambitap_convolve(const float* x, int n, const float* ir, int ir_len, int block_size, float* out) {
    if (!x || !ir || !out || n <= 0 || ir_len <= 0 || block_size < 4) {
        return -1;
    }
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

int ambitap_binaural_render(int order, float sample_rate, int magls, const float* mono, int n_frames, const float* az,
                            const float* el, float head_yaw, float head_pitch, float head_roll, float* left,
                            float* right) {
    if (!mono || !az || !el || !left || !right || n_frames <= 0) {
        return -1;
    }
    try {
        constexpr size_t block = 64;

        dsp::encoder enc(order);
        enc.set_direction(az[0], el[0]);
        enc.snap_parameters();

        dsp::binaural_renderer bin(order);
        if (magls) {
            bin.set_projection(dsp::binaural_renderer::hrtf_projection::magls);
        }
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
            const auto len = static_cast<size_t>(std::min(static_cast<int>(block), n_frames - start));
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

int ambitap_encoder_ramp(int order, float az0, float el0, float az1, float el1, int change_at, int n_frames,
                         float* out) {
    if (!out || n_frames <= 0 || change_at < 0 || change_at >= n_frames) {
        return -1;
    }
    try {
        dsp::encoder enc(order);
        enc.set_direction(az0, el0);
        enc.snap_parameters();

        const size_t       channels = enc.channels();
        std::vector<float> frame(channels);
        const auto         n = static_cast<size_t>(n_frames);
        for (size_t i = 0; i < n; ++i) {
            if (i == static_cast<size_t>(change_at)) {
                enc.set_direction(az1, el1);
            }
            enc.process_sample(1.f, frame.data());
            for (size_t ch = 0; ch < channels; ++ch) {
                out[ch * n + i] = frame[ch];
            }
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_decoder_crossfade(int order, int n_speakers, const float* az, const float* el, const char* from_algorithm,
                              const char* to_algorithm, int use_max_re, const float* hoa, int n_frames, float* out) {
    dsp::decoder_algorithm from{};
    dsp::decoder_algorithm to{};
    if (!az || !el || !hoa || !out || n_speakers <= 0 || n_frames <= 0 || !algorithm_by_name(from_algorithm, from)
        || !algorithm_by_name(to_algorithm, to)) {
        return -1;
    }
    try {
        std::vector<spherical_coord> speakers;
        speakers.reserve(static_cast<size_t>(n_speakers));
        for (int i = 0; i < n_speakers; ++i) {
            speakers.push_back({az[i], el[i]});
        }

        dsp::decoder dec(order);
        dec.set_max_re(use_max_re != 0);
        dec.set_speakers(speakers);
        dec.set_algorithm(from);
        dec.wait_for_settling();

        // Run past the initial fade-in from silence so the capture below
        // starts from the settled `from` matrix.
        const auto         n_out = static_cast<size_t>(n_speakers);
        std::vector<float> frame(n_out);
        for (size_t i = 0; i < 2 * dsp::decoder::k_fade_samples; ++i) {
            dec.process_frame(hoa, frame.data(), n_out);
        }

        // Adopt the new matrix; the crossfade begins on the next process call.
        dec.set_algorithm(to);
        dec.wait_for_settling();

        const auto n = static_cast<size_t>(n_frames);
        for (size_t i = 0; i < n; ++i) {
            dec.process_frame(hoa, frame.data(), n_out);
            for (size_t s = 0; s < n_out; ++s) {
                out[s * n + i] = frame[s];
            }
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_doppler_process(float sample_rate, float max_distance, const float* mono, const float* dist, int n_frames,
                            float* out) {
    if (!mono || !dist || !out || n_frames <= 0 || sample_rate <= 0.f || max_distance <= 0.f) {
        return -1;
    }
    try {
        dsp::doppler dop(1);
        dop.set_distance(dist[0]);
        dop.set_max_distance(max_distance);
        dop.prepare(sample_rate);
        dop.reset(); // snap the delay slew to the initial distance

        const size_t       channels = dop.channels();
        std::vector<float> fin(channels, 0.f);
        std::vector<float> fout(channels, 0.f);
        for (int i = 0; i < n_frames; ++i) {
            dop.set_distance(dist[i]);
            fin[0] = mono[i];
            dop.process_frame(fin.data(), fout.data());
            out[i] = fout[0];
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_compressor_gain(float sample_rate, float threshold_db, float ratio, float attack_s, float release_s,
                            float makeup_db, const float* w, int n_frames, float* out) {
    if (!w || !out || n_frames <= 0 || sample_rate <= 0.f || ratio < 1.f) {
        return -1;
    }
    try {
        dsp::spatial_compressor comp(1);
        comp.prepare(sample_rate);
        comp.set_threshold_db(threshold_db);
        comp.set_ratio(ratio);
        comp.set_attack(attack_s);
        comp.set_release(release_s);
        comp.set_makeup_gain_db(makeup_db);
        for (int i = 0; i < n_frames; ++i) {
            out[i] = comp.process_envelope(w[i]);
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_soundfield_grid(int order, int az_steps, float sample_rate, float smoothing_ms, const float* hoa,
                            int n_frames, int block_size, float dynamic_range_db, float* out, float* peak_db) {
    if (!hoa || !out || n_frames <= 0 || block_size <= 0 || az_steps < 4 || sample_rate <= 0.f
        || dynamic_range_db <= 0.f) {
        return -1;
    }
    try {
        analysis::soundfield_grid grid(order, az_steps);
        grid.prepare(sample_rate);
        grid.set_smoothing_time_ms(smoothing_ms);

        const size_t              channels = grid.channels();
        const auto                n        = static_cast<size_t>(n_frames);
        std::vector<const float*> ptrs(channels);
        for (int start = 0; start < n_frames; start += block_size) {
            const auto len = static_cast<size_t>(std::min(block_size, n_frames - start));
            for (size_t c = 0; c < channels; ++c) {
                ptrs[c] = hoa + c * n + static_cast<size_t>(start);
            }
            grid.process(ptrs.data(), len);
        }

        const auto img = grid.snapshot(dynamic_range_db);
        if (img.data.empty()) {
            return -1;
        }
        std::memcpy(out, img.data.data(), img.data.size() * sizeof(float));
        if (peak_db) {
            *peak_db = img.peak_db;
        }
        return 0;
    }
    catch (...) {
        return -1;
    }
}

int ambitap_energy_vector(int order, float sample_rate, float smoothing_s, const float* hoa, int n_frames, float* out) {
    if (!hoa || !out || n_frames <= 0 || order < 1 || order > k_max_order || sample_rate <= 0.f) {
        return -1;
    }
    try {
        analysis::energy_vector ev;
        ev.prepare(sample_rate);
        ev.set_smoothing_time(smoothing_s);

        const auto                n        = static_cast<size_t>(n_frames);
        const auto                channels = channel_count(order);
        std::vector<const float*> in(channels);
        for (size_t c = 0; c < channels; ++c) {
            in[c] = hoa + c * n;
        }
        float* xyz[3] = {out, out + n, out + 2 * n};
        ev.process(in.data(), xyz, n);
        return 0;
    }
    catch (...) {
        return -1;
    }
}

} // extern "C"
