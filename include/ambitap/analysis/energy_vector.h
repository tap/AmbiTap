/// AmbiTap: target-independent ambisonics library
/// Active intensity vector estimator (broadband energy vector).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_ANALYSIS_ENERGY_VECTOR_H
#define AMBITAP_ANALYSIS_ENERGY_VECTOR_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ambitap::analysis {

    /// Real-time active intensity vector for a higher-order ambisonics bus.
    ///
    /// For first-order signals (W, Y, Z, X channels), the instantaneous active
    /// intensity is
    ///     I_x[t] = W[t] · X[t]
    ///     I_y[t] = W[t] · Y[t]
    ///     I_z[t] = W[t] · Z[t]
    /// pointing from the listener toward the apparent source direction.
    ///
    /// The instantaneous WX product oscillates at 2ω, so a one-pole low-pass
    /// with a time constant of a few ms is essential to recover a usable
    /// direction estimate. Only the first four input channels are used; higher
    /// orders are accepted but ignored — sufficient for broadband DOA analysis.
    ///
    /// Output axes (standard ambisonics Cartesian): x = front+, y = left+, z = up+.
    class energy_vector {
        float                m_fs {48000.f};
        float                m_smoothing_s {0.01f};
        float                m_alpha {0.002f}; // one-pole coef, ~10 ms at 48 kHz
        std::array<float, 3> m_state {0.f, 0.f, 0.f};

      public:
        energy_vector() { recalculate(); }

        /// Set the sample rate and recompute the smoothing coefficient.
        void prepare(float sample_rate) {
            m_fs = sample_rate;
            recalculate();
        }

        /// One-pole smoothing time constant, in seconds. ~10 ms is a usable default.
        void  set_smoothing_time(float seconds) { m_smoothing_s = seconds; recalculate(); }
        float smoothing_time() const { return m_smoothing_s; }

        /// Advance the estimator by one frame (in: >= 4 HOA channels, ACN order)
        /// and write the smoothed intensity vector to out[3] (x, y, z).
        void process_frame(const float* in, float* out) {
            const float w   = in[0];
            const float i_x = w * in[3];
            const float i_y = w * in[1];
            const float i_z = w * in[2];
            m_state[0] += (i_x - m_state[0]) * m_alpha;
            m_state[1] += (i_y - m_state[1]) * m_alpha;
            m_state[2] += (i_z - m_state[2]) * m_alpha;
            out[0] = m_state[0];
            out[1] = m_state[1];
            out[2] = m_state[2];
        }

        /// Process a block: in = planar HOA channels (>= 4), out = 3 planar buffers.
        void process(const float* const* in, float* const* out, size_t frame_count) {
            const float a  = m_alpha;
            float       s0 = m_state[0];
            float       s1 = m_state[1];
            float       s2 = m_state[2];
            for (size_t i = 0; i < frame_count; ++i) {
                const float w = in[0][i];
                s0 += (w * in[3][i] - s0) * a;
                s1 += (w * in[1][i] - s1) * a;
                s2 += (w * in[2][i] - s2) * a;
                out[0][i] = s0;
                out[1][i] = s1;
                out[2][i] = s2;
            }
            m_state[0] = s0;
            m_state[1] = s1;
            m_state[2] = s2;
        }

        /// Current smoothed vector.
        const std::array<float, 3>& value() const { return m_state; }

        /// Reset the smoothing state.
        void reset() { m_state = {0.f, 0.f, 0.f}; }

      private:
        void recalculate() {
            // Standard one-pole: alpha = 1 - exp(-1 / (tau * fs)).
            const float tau = std::max(1e-6f, m_smoothing_s);
            m_alpha         = 1.f - std::exp(-1.f / (tau * m_fs));
        }
    };

} // namespace ambitap::analysis

#endif // AMBITAP_ANALYSIS_ENERGY_VECTOR_H
