/// @file virtual_mic.h
/// @brief Virtual microphone: extract a mono directional signal from a HOA bus.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <array>
#include <cstddef>

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"
#include "../math/decoding/max_re.h"
#include "util/smoothing.h"

namespace ambitap::dsp {

    /// Extract a mono signal from a HOA bus, pointing in a chosen direction.
    ///
    /// The output is a SH-domain dot product:
    ///     out[t] = sum_ch  input[ch][t] * SH_ch(azimuth, elevation) * w_n_ch
    /// where w_n is either 1 (raw "basic" pattern) or the max-rE per-order
    /// weighting (smoother sidelobes at the cost of slightly less directivity).
    ///
    /// At order 1 this gives a cardioid response; higher orders narrow the beam.
    ///
    /// Threading: setters run on one control thread, the process methods on
    /// one audio thread. Coefficient changes ramp over k_smoothing_samples;
    /// call snap_parameters() for offline/exact use. The audio path is
    /// wait-free.
    class virtual_mic {
      public:
        /// @param order  Ambisonics order in [1, k_max_order].
        /// @throws std::invalid_argument on out-of-range order.
        explicit virtual_mic(int order)
            : m_order(validated_order(order, "dsp::virtual_mic"))
            , m_channels(channel_count(order)) {
            recalculate();
            m_smooth.init(m_coefficients.data(), m_channels);
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void set_azimuth(float radians) {
            m_azimuth = radians;
            recalculate();
            publish();
        }
        float azimuth() const { return m_azimuth; }

        void set_elevation(float radians) {
            m_elevation = radians;
            recalculate();
            publish();
        }
        float elevation() const { return m_elevation; }

        void set_direction(float azimuth_radians, float elevation_radians) {
            m_azimuth   = azimuth_radians;
            m_elevation = elevation_radians;
            recalculate();
            publish();
        }

        void set_max_re(bool enabled) {
            m_max_re = enabled;
            recalculate();
            publish();
        }
        bool max_re() const { return m_max_re; }

        /// Skip the coefficient ramp: the audio thread jumps straight to the
        /// latest targets on its next call. Offline rendering / tests.
        void snap_parameters() { m_smooth.snap(); }

        float        coefficient(size_t channel) const { return m_coefficients[channel]; }
        const float* coefficients() const { return m_coefficients.data(); }

        /// Extract one mono sample from one frame of channels() input samples.
        /// Audio thread only; wait-free.
        float process_frame(const float* in) const noexcept {
            const float* c   = m_smooth.tick(m_channels);
            float        acc = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                acc += in[ch] * c[ch];
            }
            return acc;
        }

        /// Extract a mono block from planar channel buffers.
        /// Audio thread only; wait-free.
        void process(const float* const* in, float* out, size_t frame_count) const noexcept {
            for (size_t i = 0; i < frame_count; ++i) {
                const float* c   = m_smooth.tick(m_channels);
                float        acc = 0.f;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    acc += in[ch][i] * c[ch];
                }
                out[i] = acc;
            }
        }

      private:
        void recalculate() {
            float sh[k_max_channel_count];
            evaluate_sh(m_order, m_azimuth, m_elevation, sh);

            if (m_max_re) {
                const auto weights = max_re_weights(m_order);
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    m_coefficients[ch] = sh[ch] * weights[static_cast<size_t>(acn_order(ch))];
                }
            }
            else {
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    m_coefficients[ch] = sh[ch];
                }
            }
        }

        void publish() { m_smooth.set(m_coefficients.data(), m_channels); }

        int    m_order;
        size_t m_channels;
        float  m_azimuth{0.0f};
        float  m_elevation{0.0f};
        bool   m_max_re{false};

        std::array<float, k_max_channel_count> m_coefficients{};

        smoothed_table<k_max_channel_count> m_smooth;
    };

} // namespace ambitap::dsp
