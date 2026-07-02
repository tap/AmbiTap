/// AmbiTap: target-independent ambisonics library
/// FuMa <-> AmbiX channel ordering and normalization conversion.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_FORMAT_CONVERTER_H
#define AMBITAP_DSP_FORMAT_CONVERTER_H

#include "../math/core/indexing.h"
#include "../math/core/validate.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace ambitap::dsp {

    /// Conversion direction for format_converter.
    enum class format_direction {
        ambix_to_fuma,
        fuma_to_ambix,
    };

    /// Convert ambisonics signals between FuMa (Furse-Malham, legacy B-format) and
    /// AmbiX (ACN ordering + SN3D normalization).
    ///
    /// Conversion has two parts:
    ///   1. Channel permutation — FuMa orders the channels W, X, Y, Z, R, S, T,
    ///      U, V, K, L, M, N, O, P, Q. ACN orders them as the spherical-harmonic
    ///      indices n^2 + n + m.
    ///   2. Per-channel gain — FuMa "maxN" normalization differs from SN3D. The
    ///      factors are from Daniel/Polack's standard FuMa-SN3D conversion table.
    ///
    /// Supported orders: 0..3 (FuMa's canonical range — there's no agreed-upon
    /// FuMa convention for higher orders).
    class format_converter {
      public:
        static constexpr size_t max_channels = 16;

      private:
        size_t           m_channels;
        format_direction m_direction {format_direction::ambix_to_fuma};

        // Per-output-channel (input_index, gain) tables, recomputed on direction change.
        std::array<size_t, max_channels> m_input_index {};
        std::array<float, max_channels>  m_input_gain {};

        // FuMa channel order: W, X, Y, Z, R, S, T, U, V, K, L, M, N, O, P, Q.
        // Maps FuMa index -> ACN index.
        static constexpr std::array<size_t, max_channels> k_fuma_to_acn = {
            0,  3,  1,  2,
            6,  7,  5,  8,  4,
            12, 13, 11, 14, 10, 15, 9
        };

        // Per-(n, |m|) factor multiplied when going FROM FuMa TO SN3D; the
        // inverse direction uses the reciprocal.
        static float fuma_to_sn3d_gain(int n, int abs_m) {
            if (n == 0) return std::sqrt(2.f);
            if (n == 1) return 1.f;
            if (n == 2) {
                if (abs_m == 0) return 1.f;
                return std::sqrt(3.f) * 0.5f;
            }
            // n == 3
            if (abs_m == 0) return 1.f;
            if (abs_m == 1) return std::sqrt(45.f / 32.f);
            if (abs_m == 2) return 3.f / std::sqrt(5.f);
            return std::sqrt(8.f / 5.f);
        }

      public:
        /// @param order  Ambisonics order in [0, 3]; channel count is (order+1)^2.
        /// @throws std::invalid_argument on out-of-range order (FuMa is only
        ///         defined through order 3).
        explicit format_converter(int order)
            : m_channels(channel_count(validated_order(order, 0, 3, "dsp::format_converter"))) {
            recalculate();
        }

        size_t channels() const { return m_channels; }

        void set_direction(format_direction direction) {
            m_direction = direction;
            recalculate();
        }
        format_direction direction() const { return m_direction; }

        /// For output channel `out_ch`, the input channel it is sourced from.
        size_t input_index(size_t out_ch) const { return m_input_index[out_ch]; }
        /// For output channel `out_ch`, the gain applied to that input channel.
        float input_gain(size_t out_ch) const { return m_input_gain[out_ch]; }

        /// Convert one frame of channels() samples. Output must not alias input.
        void process_frame(const float* in, float* out) const {
            for (size_t out_ch = 0; out_ch < m_channels; ++out_ch) {
                out[out_ch] = in[m_input_index[out_ch]] * m_input_gain[out_ch];
            }
        }

        /// Convert a block of planar channel buffers. Output must not alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) const {
            for (size_t out_ch = 0; out_ch < m_channels; ++out_ch) {
                const float* src = in[m_input_index[out_ch]];
                const float  g   = m_input_gain[out_ch];
                float*       dst = out[out_ch];
                for (size_t i = 0; i < frame_count; ++i) {
                    dst[i] = src[i] * g;
                }
            }
        }

      private:
        void recalculate() {
            // The inverse permutation (ACN -> FuMa) over the active channels.
            std::array<size_t, max_channels> acn_to_fuma {};
            for (size_t i = 0; i < m_channels; ++i) acn_to_fuma[k_fuma_to_acn[i]] = i;

            for (size_t out_ch = 0; out_ch < m_channels; ++out_ch) {
                if (m_direction == format_direction::fuma_to_ambix) {
                    // Output is ACN-ordered; input is FuMa-ordered. Pull from
                    // FuMa position acn_to_fuma[out_ch] and scale up to SN3D.
                    const int n           = acn_order(out_ch);
                    const int abs_m       = std::abs(acn_degree(out_ch));
                    m_input_index[out_ch] = acn_to_fuma[out_ch];
                    m_input_gain[out_ch]  = fuma_to_sn3d_gain(n, abs_m);
                }
                else {
                    // Output is FuMa-ordered; input is ACN-ordered. The output
                    // channel at FuMa position out_ch corresponds to ACN
                    // k_fuma_to_acn[out_ch]; scale down from SN3D to FuMa.
                    const size_t input_acn = k_fuma_to_acn[out_ch];
                    const int    n         = acn_order(input_acn);
                    const int    abs_m     = std::abs(acn_degree(input_acn));
                    m_input_index[out_ch]  = input_acn;
                    m_input_gain[out_ch]   = 1.f / fuma_to_sn3d_gain(n, abs_m);
                }
            }
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_FORMAT_CONVERTER_H
