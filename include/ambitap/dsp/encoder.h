/// AmbiTap: target-independent ambisonics library
/// Point-source HOA encoder processor.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_ENCODER_H
#define AMBITAP_DSP_ENCODER_H

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"

#include <array>
#include <cstddef>

namespace ambitap::dsp {

    /// Encode a mono source as a point-source at a given direction into a HOA bus
    /// (ACN ordering, SN3D normalization).
    ///
    /// SH coefficients are recomputed only when the direction changes; the audio
    /// path is a single multiply per output channel per sample and is real-time
    /// safe (no allocation, locks, or syscalls).
    class encoder {
        int    m_order;
        size_t m_channels;
        float  m_azimuth {0.0f};
        float  m_elevation {0.0f};
        float  m_gain {1.0f};

        std::array<float, max_channel_count> m_coefficients {};

      public:
        /// @param order  Ambisonics order in [0, max_order]; channel count is (order+1)^2.
        explicit encoder(int order)
            : m_order(order)
            , m_channels(channel_count(order)) {
            recalculate();
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        /// Source azimuth in radians. 0 = front, pi/2 = left.
        void  set_azimuth(float radians) { m_azimuth = radians; recalculate(); }
        float azimuth() const { return m_azimuth; }

        /// Source elevation in radians. 0 = horizon, pi/2 = zenith.
        void  set_elevation(float radians) { m_elevation = radians; recalculate(); }
        float elevation() const { return m_elevation; }

        /// Set both angles with a single coefficient recalculation.
        void set_direction(float azimuth_radians, float elevation_radians) {
            m_azimuth   = azimuth_radians;
            m_elevation = elevation_radians;
            recalculate();
        }

        /// Linear output gain, folded into the per-channel coefficients.
        void  set_gain(float linear) { m_gain = linear; }
        float gain() const { return m_gain; }

        /// Raw SH coefficients for the current direction (gain not applied).
        const float* coefficients() const { return m_coefficients.data(); }

        /// Per-channel multiplier including gain: out[ch] = in * channel_gain(ch).
        float channel_gain(size_t channel) const { return m_coefficients[channel] * m_gain; }

        /// Encode one mono sample into channels() output samples.
        void process_sample(float in, float* out) const {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in * m_coefficients[ch] * m_gain;
            }
        }

        /// Encode a mono block into channels() planar output buffers.
        void process(const float* in, float* const* out, size_t frame_count) const {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                const float c   = m_coefficients[ch] * m_gain;
                float*      dst = out[ch];
                for (size_t i = 0; i < frame_count; ++i) {
                    dst[i] = in[i] * c;
                }
            }
        }

      private:
        void recalculate() { evaluate_sh(m_order, m_azimuth, m_elevation, m_coefficients.data()); }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_ENCODER_H
