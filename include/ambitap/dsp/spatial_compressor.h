/// AmbiTap: target-independent ambisonics library
/// Omnidirectional spatial-preserving compressor for higher-order ambisonics.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_SPATIAL_COMPRESSOR_H
#define AMBITAP_DSP_SPATIAL_COMPRESSOR_H

#include "../math/core/indexing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ambitap::dsp {

    /// Compress a higher-order ambisonics signal without altering its spatial
    /// image: detect level on the W (omni) channel only, then apply the same
    /// time-varying gain to every channel.
    ///
    /// Because every SH channel gets multiplied by the same scalar at each sample,
    /// the directional structure of the soundfield is exactly preserved — only
    /// total amplitude changes.
    ///
    /// Lifecycle: construct with the ambisonics order, call prepare() with the
    /// sample rate (recomputes the envelope coefficients; defaults assume
    /// 48 kHz). process() is real-time safe.
    class spatial_compressor {
        size_t m_channels;
        float  m_fs {48000.f};
        float  m_threshold_db {-12.f};
        float  m_ratio {4.f};
        float  m_attack_s {0.005f};
        float  m_release_s {0.1f};
        float  m_makeup_db {0.f};

        float m_envelope {0.f};
        float m_attack_coef {0.f};
        float m_release_coef {0.f};

      public:
        /// @param order  Ambisonics order in [1, max_order].
        explicit spatial_compressor(int order)
            : m_channels(channel_count(order)) {
            recalculate();
        }

        size_t channels() const { return m_channels; }

        /// Set the sample rate and recompute envelope coefficients.
        void prepare(float sample_rate) {
            m_fs = sample_rate;
            recalculate();
        }

        /// Compression threshold in dB.
        void  set_threshold_db(float db) { m_threshold_db = db; }
        float threshold_db() const { return m_threshold_db; }

        /// Compression ratio. 1 = no compression.
        void  set_ratio(float ratio) { m_ratio = ratio; }
        float ratio() const { return m_ratio; }

        /// Envelope-follower attack time in seconds.
        void  set_attack(float seconds) { m_attack_s = seconds; recalculate(); }
        float attack() const { return m_attack_s; }

        /// Envelope-follower release time in seconds.
        void  set_release(float seconds) { m_release_s = seconds; recalculate(); }
        float release() const { return m_release_s; }

        /// Makeup gain applied after compression, in dB.
        void  set_makeup_gain_db(float db) { m_makeup_db = db; }
        float makeup_gain_db() const { return m_makeup_db; }

        /// Update the envelope follower with one W sample, then return the
        /// linear gain to apply to every channel (compression + makeup).
        float process_envelope(float w_sample) {
            const float abs_w = std::abs(w_sample);
            const float coef  = (abs_w > m_envelope) ? m_attack_coef : m_release_coef;
            m_envelope += (abs_w - m_envelope) * coef;

            const float env_db    = 20.f * std::log10(std::max(m_envelope, 1e-12f));
            const float over      = env_db - m_threshold_db;
            const float reduce_db = (over > 0.f) ? -over * (1.f - 1.f / m_ratio) : 0.f;
            const float total_db  = reduce_db + m_makeup_db;
            return std::pow(10.f, total_db / 20.f);
        }

        /// Process a block of planar channel buffers (W = channel 0).
        /// Output may alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) {
            for (size_t i = 0; i < frame_count; ++i) {
                const float gain = process_envelope(in[0][i]);
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][i] = in[ch][i] * gain;
                }
            }
        }

        /// Reset the envelope follower.
        void reset() { m_envelope = 0.f; }

      private:
        void recalculate() {
            const float a  = std::max(1e-6f, m_attack_s);
            const float r  = std::max(1e-6f, m_release_s);
            m_attack_coef  = 1.f - std::exp(-1.f / (a * m_fs));
            m_release_coef = 1.f - std::exp(-1.f / (r * m_fs));
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_SPATIAL_COMPRESSOR_H
