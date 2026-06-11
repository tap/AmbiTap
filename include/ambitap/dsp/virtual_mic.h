/// AmbiTap: target-independent ambisonics library
/// Virtual microphone: extract a mono directional signal from a HOA bus.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_VIRTUAL_MIC_H
#define AMBITAP_DSP_VIRTUAL_MIC_H

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/decoding/max_re.h"

#include <array>
#include <cstddef>

namespace ambitap::dsp {

    /// Extract a mono signal from a HOA bus, pointing in a chosen direction.
    ///
    /// The output is a SH-domain dot product:
    ///     out[t] = sum_ch  input[ch][t] * SH_ch(azimuth, elevation) * w_n_ch
    /// where w_n is either 1 (raw "basic" pattern) or the max-rE per-order
    /// weighting (smoother sidelobes at the cost of slightly less directivity).
    ///
    /// At order 1 this gives a cardioid response; higher orders narrow the beam.
    class virtual_mic {
        int    m_order;
        size_t m_channels;
        float  m_azimuth {0.0f};
        float  m_elevation {0.0f};
        bool   m_max_re {false};

        std::array<float, max_channel_count> m_coefficients {};

      public:
        /// @param order  Ambisonics order in [1, max_order].
        explicit virtual_mic(int order)
            : m_order(order)
            , m_channels(channel_count(order)) {
            recalculate();
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void  set_azimuth(float radians) { m_azimuth = radians; recalculate(); }
        float azimuth() const { return m_azimuth; }

        void  set_elevation(float radians) { m_elevation = radians; recalculate(); }
        float elevation() const { return m_elevation; }

        void set_direction(float azimuth_radians, float elevation_radians) {
            m_azimuth   = azimuth_radians;
            m_elevation = elevation_radians;
            recalculate();
        }

        void set_max_re(bool enabled) { m_max_re = enabled; recalculate(); }
        bool max_re() const { return m_max_re; }

        float        coefficient(size_t channel) const { return m_coefficients[channel]; }
        const float* coefficients() const { return m_coefficients.data(); }

        /// Extract one mono sample from one frame of channels() input samples.
        float process_frame(const float* in) const {
            float acc = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                acc += in[ch] * m_coefficients[ch];
            }
            return acc;
        }

        /// Extract a mono block from planar channel buffers.
        void process(const float* const* in, float* out, size_t frame_count) const {
            for (size_t i = 0; i < frame_count; ++i) {
                float acc = 0.f;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    acc += in[ch][i] * m_coefficients[ch];
                }
                out[i] = acc;
            }
        }

      private:
        void recalculate() {
            float sh[max_channel_count];
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
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_VIRTUAL_MIC_H
