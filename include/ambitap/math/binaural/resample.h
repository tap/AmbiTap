/// @file resample.h
/// @brief Offline FIR resampling (windowed sinc) for HRTF sample-rate adaptation.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace ambitap {

    /// Resample a FIR from in_rate to out_rate with a Hann-windowed sinc
    /// kernel (32 input samples per side). Offline use only — allocates;
    /// intended for adapting HRTF FIRs to the host sample rate at prepare()
    /// time, not for streaming audio.
    ///
    /// Unity passband gain; when downsampling, the kernel cutoff is lowered
    /// to the output Nyquist to anti-alias.
    inline std::vector<float> resample_fir(const float* in, size_t in_len, float in_rate, float out_rate) {
        if (in_len == 0) {
            return {};
        }
        if (in_rate == out_rate) {
            return {in, in + in_len};
        }

        const double ratio   = static_cast<double>(out_rate) / static_cast<double>(in_rate);
        const auto   out_len = static_cast<size_t>(std::ceil(static_cast<double>(in_len) * ratio));
        const double fc      = std::min(1.0, ratio); // input-Nyquist fraction
        const int    half    = 32;
        const double pi      = 3.14159265358979323846;

        auto kernel = [&](double x) -> double {
            const double ax = std::abs(x);
            if (ax >= half) {
                return 0.0;
            }
            const double window = 0.5 * (1.0 + std::cos(pi * ax / half));
            if (ax < 1e-9) {
                return fc * window;
            }
            return fc * std::sin(pi * fc * x) / (pi * fc * x) * window;
        };

        std::vector<float> out(out_len, 0.f);
        for (size_t n = 0; n < out_len; ++n) {
            const double center = static_cast<double>(n) / ratio;
            const auto   j0     = static_cast<long>(std::floor(center)) - half + 1;
            double       acc    = 0.0;
            for (long j = j0; j < j0 + 2 * half; ++j) {
                if (j < 0 || j >= static_cast<long>(in_len)) {
                    continue;
                }
                acc += static_cast<double>(in[static_cast<size_t>(j)]) * kernel(center - static_cast<double>(j));
            }
            out[n] = static_cast<float>(acc);
        }
        return out;
    }

} // namespace ambitap
