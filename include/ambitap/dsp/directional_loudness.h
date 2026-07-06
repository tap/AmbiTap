/// AmbiTap: target-independent ambisonics library
/// Apply directional gain to a higher-order ambisonics signal.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H
#define AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H

#include <array>
#include <cstddef>

#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"
#include "util/smoothing.h"

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
    ///
    /// Threading: setters run on one control thread, the process methods on
    /// one audio thread. Direction and gain changes ramp over
    /// k_smoothing_samples; call snap_parameters() for offline/exact use. The
    /// audio path is wait-free.
    class directional_loudness {
        int                                  m_order;
        size_t                               m_channels;
        float                                m_azimuth{0.0f};
        float                                m_elevation{0.0f};
        std::array<float, k_max_channel_count> m_sh_at_direction{};

        smoothed_table<k_max_channel_count> m_smooth;
        smoothed_scalar                   m_gain{1.0f};
        // 1 / ||SH(d)||^2, recomputed with the direction.
        smoothed_scalar m_inv_norm{1.0f};

      public:
        /// @param order  Ambisonics order in [0, k_max_order].
        /// @throws std::invalid_argument on out-of-range order.
        explicit directional_loudness(int order)
            : m_order(validated_order(order, "dsp::directional_loudness"))
            , m_channels(channel_count(order)) {
            recalculate();
            m_smooth.init(m_sh_at_direction.data(), m_channels);
            m_inv_norm.snap(); // no ramp from the default-constructed value
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

        /// Linear gain achieved at the look direction. 1 = bypass, 0 = remove,
        /// >1 = boost.
        void  set_gain(float linear) { m_gain.set(linear); }
        float gain() const { return m_gain.target(); }

        /// Skip the parameter ramps: the audio thread jumps straight to the
        /// latest targets on its next call. Offline rendering / tests.
        void snap_parameters() {
            m_smooth.snap();
            m_gain.snap();
            m_inv_norm.snap();
        }

        float        sh_coefficient(size_t channel) const { return m_sh_at_direction[channel]; }
        const float* sh_coefficients() const { return m_sh_at_direction.data(); }

        /// Process one frame of channels() samples. Output may alias input.
        /// Audio thread only; wait-free.
        void process_frame(const float* in, float* out) const noexcept {
            const float* sh        = m_smooth.tick(m_channels);
            const float  gain      = m_gain.tick();
            const float  inv_norm  = m_inv_norm.tick();
            float        extracted = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                extracted += in[ch] * sh[ch];
            }
            const float delta = (gain - 1.f) * extracted * inv_norm;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in[ch] + delta * sh[ch];
            }
        }

        /// Process a block of planar channel buffers. Output may alias input.
        /// Audio thread only; wait-free.
        void process(const float* const* in, float* const* out, size_t frame_count) const noexcept {
            for (size_t i = 0; i < frame_count; ++i) {
                const float* sh        = m_smooth.tick(m_channels);
                const float  gain      = m_gain.tick();
                const float  inv_norm  = m_inv_norm.tick();
                float        extracted = 0.f;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    extracted += in[ch][i] * sh[ch];
                }
                const float delta = (gain - 1.f) * extracted * inv_norm;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][i] = in[ch][i] + delta * sh[ch];
                }
            }
        }

      private:
        void recalculate() {
            float sh[k_max_channel_count];
            evaluate_sh(m_order, m_azimuth, m_elevation, sh);
            float norm = 0.f;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                m_sh_at_direction[ch] = sh[ch];
                norm += sh[ch] * sh[ch];
            }
            // ||SH_sn3d(d)||^2 == order+1 for any direction; computed from the
            // actual coefficients for robustness.
            m_inv_norm.set((norm > 0.f) ? 1.f / norm : 1.f);
        }

        void publish() { m_smooth.set(m_sh_at_direction.data(), m_channels); }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_DIRECTIONAL_LOUDNESS_H
