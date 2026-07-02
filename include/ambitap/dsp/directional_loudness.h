/// AmbiTap: target-independent ambisonics library
/// Apply directional gain to a higher-order ambisonics signal.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H
#define AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"

#include <array>
#include <cstddef>

namespace ambitap::dsp {

    /// Apply a directional gain to a higher-order ambisonics signal.
    ///
    /// Per audio sample:
    ///     extracted   = sum_ch  in[ch] * SH_ch(d)
    ///     out[ch]     = in[ch] + (gain - 1) * (extracted / ||SH(d)||^2) * SH_ch(d)
    ///
    /// The "extract at direction d, scale, re-encode at d, add back" pattern is
    /// the standard practical implementation of directional gain. The
    /// 1 / ||SH(d)||^2 factor calibrates the beam so a source exactly at the
    /// look direction achieves exactly the requested gain (without it the
    /// achieved gain would be 1 + (gain-1)(order+1) — inverted for gain < 1):
    ///   gain = 1: bypass (out == in)
    ///   gain > 1: boost the directionally-extracted component
    ///   gain < 1: attenuate that component (0 = remove at the look direction)
    class directional_loudness {
        int    m_order;
        size_t m_channels;
        float  m_azimuth {0.0f};
        float  m_elevation {0.0f};
        float  m_gain {1.0f};
        float  m_inv_norm {1.0f}; // 1 / ||SH(d)||^2, recomputed with the direction

        std::array<float, max_channel_count> m_sh_at_direction {};

      public:
        /// @param order  Ambisonics order in [0, max_order].
        /// @throws std::invalid_argument on out-of-range order.
        explicit directional_loudness(int order)
            : m_order(validated_order(order, "dsp::directional_loudness"))
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

        /// Linear gain achieved at the look direction. 1 = bypass, 0 = remove,
        /// >1 = boost.
        void  set_gain(float linear) { m_gain = linear; }
        float gain() const { return m_gain; }

        float        sh_coefficient(size_t channel) const { return m_sh_at_direction[channel]; }
        const float* sh_coefficients() const { return m_sh_at_direction.data(); }

        /// Process one frame of channels() samples. Output may alias input.
        void process_frame(const float* in, float* out) const {
            float extracted = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                extracted += in[ch] * m_sh_at_direction[ch];
            }
            const float delta = (m_gain - 1.f) * extracted * m_inv_norm;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in[ch] + delta * m_sh_at_direction[ch];
            }
        }

        /// Process a block of planar channel buffers. Output may alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) const {
            for (size_t i = 0; i < frame_count; ++i) {
                float extracted = 0.f;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    extracted += in[ch][i] * m_sh_at_direction[ch];
                }
                const float delta = (m_gain - 1.f) * extracted * m_inv_norm;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][i] = in[ch][i] + delta * m_sh_at_direction[ch];
                }
            }
        }

      private:
        void recalculate() {
            float sh[max_channel_count];
            evaluate_sh(m_order, m_azimuth, m_elevation, sh);
            float norm = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                m_sh_at_direction[ch] = sh[ch];
                norm += sh[ch] * sh[ch];
            }
            // ||SH_sn3d(d)||^2 == order+1 for any direction; computed from the
            // actual coefficients for robustness.
            m_inv_norm = (norm > 0.f) ? 1.f / norm : 1.f;
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H
