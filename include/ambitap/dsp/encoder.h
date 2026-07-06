/// @file encoder.h
/// @brief Point-source HOA encoder processor.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <array>
#include <cstddef>

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"
#include "util/smoothing.h"

namespace ambitap::dsp {

    /// Encode a mono source as a point-source at a given direction into a HOA bus
    /// (ACN ordering, SN3D normalization).
    ///
    /// Threading: setters run on one control thread, the process methods on one
    /// audio thread. Coefficient and gain changes are stored element-atomically
    /// and ramped in over k_smoothing_samples on the audio thread — no torn
    /// tables, no clicks. Call snap_parameters() for offline/exact use. The
    /// audio path is wait-free (no allocation, locks, or syscalls).
    class encoder {
      public:
        /// @param order  Ambisonics order in [0, k_max_order]; channel count is (order+1)^2.
        /// @throws std::invalid_argument on out-of-range order.
        explicit encoder(int order)
            : m_order(validated_order(order, "dsp::encoder"))
            , m_channels(channel_count(order)) {
            recalculate();
            m_smooth.init(m_coefficients.data(), m_channels);
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        /// Source azimuth in radians. 0 = front, pi/2 = left.
        void set_azimuth(float radians) {
            m_azimuth = radians;
            recalculate();
            publish();
        }
        float azimuth() const { return m_azimuth; }

        /// Source elevation in radians. 0 = horizon, pi/2 = zenith.
        void set_elevation(float radians) {
            m_elevation = radians;
            recalculate();
            publish();
        }
        float elevation() const { return m_elevation; }

        /// Set both angles with a single coefficient recalculation.
        void set_direction(float azimuth_radians, float elevation_radians) {
            m_azimuth   = azimuth_radians;
            m_elevation = elevation_radians;
            recalculate();
            publish();
        }

        /// Linear output gain, applied per channel. Ramped like the coefficients.
        void  set_gain(float linear) { m_gain.set(linear); }
        float gain() const { return m_gain.target(); }

        /// Raw SH coefficients for the current direction (gain not applied;
        /// target values, not the audio thread's ramp state). Control thread.
        const float* coefficients() const { return m_coefficients.data(); }

        /// Per-channel target multiplier including gain. Control thread.
        float channel_gain(size_t channel) const { return m_coefficients[channel] * m_gain.target(); }

        /// Skip the parameter ramps: the audio thread jumps straight to the
        /// latest targets on its next call. Offline rendering / tests.
        void snap_parameters() {
            m_smooth.snap();
            m_gain.snap();
        }

        /// Encode one mono sample into channels() output samples. Audio thread.
        void process_sample(float in, float* out) const noexcept {
            const float* c = m_smooth.tick(m_channels);
            const float  g = m_gain.tick();
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in * c[ch] * g;
            }
        }

        /// Encode a mono block into channels() planar output buffers. Audio thread.
        void process(const float* in, float* const* out, size_t frame_count) const noexcept {
            if (m_smooth.settled() && m_gain.settled()) {
                // Fast path: constant coefficients, channel-major loop.
                const float* c = m_smooth.tick(m_channels);
                const float  g = m_gain.tick();
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    const float k   = c[ch] * g;
                    float*      dst = out[ch];
                    for (size_t i = 0; i < frame_count; ++i) {
                        dst[i] = in[i] * k;
                    }
                }
                return;
            }
            for (size_t i = 0; i < frame_count; ++i) {
                const float* c = m_smooth.tick(m_channels);
                const float  g = m_gain.tick();
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][i] = in[i] * c[ch] * g;
                }
            }
        }

      private:
        void recalculate() { evaluate_sh(m_order, m_azimuth, m_elevation, m_coefficients.data()); }
        void publish() { m_smooth.set(m_coefficients.data(), m_channels); }

        int    m_order;
        size_t m_channels;
        float  m_azimuth{0.0f};
        float  m_elevation{0.0f};

        // Control-side snapshot for the coefficient accessors.
        std::array<float, k_max_channel_count> m_coefficients{};

        // Audio-side smoothed values.
        smoothed_table<k_max_channel_count> m_smooth;
        smoothed_scalar                     m_gain{1.0f};
    };

} // namespace ambitap::dsp
