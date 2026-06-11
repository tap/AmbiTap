/// AmbiTap: target-independent ambisonics library
/// Multi-channel variable delay for HOA — models distance-based time-of-flight
/// (and the associated Doppler shift when distance is modulated).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_DOPPLER_H
#define AMBITAP_DSP_DOPPLER_H

#include "../math/core/indexing.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace ambitap::dsp {

    /// Apply a variable propagation delay to a higher-order ambisonics signal.
    ///
    /// Models the time-of-flight from a source to the listener. When the distance
    /// is modulated over time, the per-sample delay change produces the standard
    /// Doppler frequency shift. A single delay value is applied uniformly to all
    /// channels — the spatial encoding is preserved; only arrival time changes.
    ///
    /// Lifecycle: construct with the ambisonics order, then call prepare() with
    /// the sample rate (allocates the delay buffers, sized by max_distance).
    /// Until prepare() is called the processor outputs silence. process() is
    /// real-time safe after prepare(); set_distance() and set_speed_of_sound()
    /// never allocate. set_max_distance() and prepare() reallocate.
    ///
    /// Reads use linear interpolation between adjacent samples for smooth
    /// modulation.
    class doppler {
        size_t m_channels;
        float  m_fs {48000.0f};
        float  m_distance {1.0f};
        float  m_speed_of_sound {343.0f};
        float  m_max_distance {50.0f};

        size_t                          m_buffer_size {0};
        std::vector<std::vector<float>> m_buffers; // [channel][circular sample]
        size_t                          m_write_idx {0};

      public:
        /// @param order  Ambisonics order in [1, max_order].
        explicit doppler(int order)
            : m_channels(channel_count(order)) {}

        size_t channels() const { return m_channels; }
        bool   is_prepared() const { return m_buffer_size != 0; }

        /// Set the sample rate and (re)allocate the delay buffers. Not RT-safe.
        void prepare(float sample_rate) {
            m_fs = sample_rate;
            allocate();
        }

        /// Source-to-listener distance in meters. Modulate for Doppler. RT-safe.
        void  set_distance(float meters) { m_distance = meters; }
        float distance() const { return m_distance; }

        /// Speed of sound in m/s. RT-safe (does not resize the buffer).
        void  set_speed_of_sound(float meters_per_second) { m_speed_of_sound = meters_per_second; }
        float speed_of_sound() const { return m_speed_of_sound; }

        /// Maximum allowed distance — sizes the delay buffer. Reallocates.
        void set_max_distance(float meters) {
            m_max_distance = meters;
            allocate();
        }
        float max_distance() const { return m_max_distance; }

        /// Current delay in samples, clamped to the buffer.
        float current_delay_samples() const {
            float s = m_distance / m_speed_of_sound * m_fs;
            // Cap at buffer_size - 2 so the interpolation can read i and i+1.
            const float max_s = static_cast<float>(m_buffer_size) - 2.f;
            if (s < 0.f) s = 0.f;
            if (s > max_s) s = max_s;
            return s;
        }

        /// Process one frame of channels() samples. Output may alias input.
        /// Outputs silence until prepare() has been called.
        void process_frame(const float* in, float* out) {
            if (m_buffer_size == 0) {
                for (size_t ch = 0; ch < m_channels; ++ch) out[ch] = 0.f;
                return;
            }
            const float delay_samples = current_delay_samples();
            for (size_t ch = 0; ch < m_channels; ++ch) {
                m_buffers[ch][m_write_idx] = in[ch];
                out[ch] = read_interpolated(m_buffers[ch], m_write_idx, delay_samples);
            }
            m_write_idx = (m_write_idx + 1) % m_buffer_size;
        }

        /// Process a block of planar channel buffers. Output may alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) {
            if (m_buffer_size == 0) {
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    for (size_t i = 0; i < frame_count; ++i) out[ch][i] = 0.f;
                }
                return;
            }
            const float delay_samples = current_delay_samples();
            for (size_t i = 0; i < frame_count; ++i) {
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    m_buffers[ch][m_write_idx] = in[ch][i];
                    out[ch][i] = read_interpolated(m_buffers[ch], m_write_idx, delay_samples);
                }
                m_write_idx = (m_write_idx + 1) % m_buffer_size;
            }
        }

        /// Clear the delay lines; keep the allocation.
        void reset() {
            for (auto& buf : m_buffers) std::fill(buf.begin(), buf.end(), 0.f);
            m_write_idx = 0;
        }

      private:
        static float read_interpolated(const std::vector<float>& buf,
                                       size_t                    write_idx,
                                       float                     delay_samples) {
            const size_t N         = buf.size();
            const float  idx_f     = static_cast<float>(write_idx) - delay_samples;
            const float  wrapped_f = idx_f - std::floor(idx_f / static_cast<float>(N))
                                    * static_cast<float>(N);
            const size_t i0   = static_cast<size_t>(wrapped_f) % N;
            const size_t i1   = (i0 + 1) % N;
            const float  frac = wrapped_f - std::floor(wrapped_f);
            return buf[i0] * (1.f - frac) + buf[i1] * frac;
        }

        void allocate() {
            // +2 samples of headroom for the interpolator.
            const size_t needed =
                static_cast<size_t>(std::ceil(m_max_distance / m_speed_of_sound * m_fs)) + 2;
            if (needed == m_buffer_size) return;

            m_buffer_size = needed;
            m_buffers.assign(m_channels, std::vector<float>(m_buffer_size, 0.f));
            m_write_idx = 0;
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_DOPPLER_H
