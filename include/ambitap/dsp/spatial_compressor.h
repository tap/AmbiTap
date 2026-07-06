/// @file spatial_compressor.h
/// @brief Omnidirectional spatial-preserving compressor for higher-order ambisonics.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "../math/core/fast_math.h"
#include "../math/core/indexing.h"
#include "../math/core/validate.h"

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
    ///
    /// Threading: setters run on one control thread (atomic stores — no torn
    /// values), the process methods on one audio thread.
    class spatial_compressor {
      public:
        /// @param order  Ambisonics order in [1, k_max_order].
        /// @throws std::invalid_argument on out-of-range order.
        explicit spatial_compressor(int order)
            : m_channels(channel_count(validated_order(order, "dsp::spatial_compressor"))) {
            recalculate();
        }

        size_t channels() const { return m_channels; }

        /// Set the sample rate and recompute envelope coefficients.
        void prepare(float sample_rate) {
            m_fs = sample_rate;
            recalculate();
        }

        /// Compression threshold in dB.
        void set_threshold_db(float db) {
            m_threshold_db.store(db, std::memory_order_relaxed);
            // Linear mirror, so the audio thread's below-threshold fast path
            // is a single compare (exact libm here; control thread).
            m_threshold_lin.store(std::pow(10.f, db / 20.f), std::memory_order_relaxed);
        }
        float threshold_db() const { return m_threshold_db.load(std::memory_order_relaxed); }

        /// Compression ratio. 1 = no compression.
        void  set_ratio(float ratio) { m_ratio.store(ratio, std::memory_order_relaxed); }
        float ratio() const { return m_ratio.load(std::memory_order_relaxed); }

        /// Envelope-follower attack time in seconds.
        void set_attack(float seconds) {
            m_attack_s = seconds;
            recalculate();
        }
        float attack() const { return m_attack_s; }

        /// Envelope-follower release time in seconds.
        void set_release(float seconds) {
            m_release_s = seconds;
            recalculate();
        }
        float release() const { return m_release_s; }

        /// Makeup gain applied after compression, in dB.
        void  set_makeup_gain_db(float db) { m_makeup_db.store(db, std::memory_order_relaxed); }
        float makeup_gain_db() const { return m_makeup_db.load(std::memory_order_relaxed); }

        /// Update the envelope follower with one W sample, then return the
        /// linear gain to apply to every channel (compression + makeup).
        ///
        /// Uses fast_log2/fast_exp2 (error < 1e-4 dB) instead of per-sample
        /// libm — std::log10 + std::pow here cost hundreds of cycles each on
        /// FPUs without hardware doubles. Below threshold the gain needs no
        /// transcendentals at all: a single linear-domain compare.
        float process_envelope(float w_sample) noexcept {
            const float abs_w = std::abs(w_sample);
            const float coef  = (abs_w > m_envelope) ? m_attack_coef.load(std::memory_order_relaxed)
                                                     : m_release_coef.load(std::memory_order_relaxed);
            m_envelope += (abs_w - m_envelope) * coef;
            if (m_envelope < 1e-30f) {
                m_envelope = 0.f; // denormal guard
            }

            const float makeup_db = m_makeup_db.load(std::memory_order_relaxed);
            if (m_envelope <= m_threshold_lin.load(std::memory_order_relaxed)) {
                return (makeup_db == 0.f) ? 1.f : fast_linear_from_db(makeup_db);
            }
            const float env_db    = fast_db_from_linear(std::max(m_envelope, 1e-12f));
            const float over      = env_db - m_threshold_db.load(std::memory_order_relaxed);
            const float ratio     = m_ratio.load(std::memory_order_relaxed);
            const float reduce_db = (over > 0.f) ? -over * (1.f - 1.f / ratio) : 0.f;
            const float total_db  = reduce_db + makeup_db;
            return fast_linear_from_db(total_db);
        }

        /// Process a block of planar channel buffers (W = channel 0).
        /// Output may alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) noexcept {
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
            const float a = std::max(1e-6f, m_attack_s);
            const float r = std::max(1e-6f, m_release_s);
            m_attack_coef.store(1.f - std::exp(-1.f / (a * m_fs)), std::memory_order_relaxed);
            m_release_coef.store(1.f - std::exp(-1.f / (r * m_fs)), std::memory_order_relaxed);
        }

        size_t m_channels;
        float  m_fs{48000.f}; // control thread only (prepare)
        float  m_attack_s{0.005f};
        float  m_release_s{0.1f};

        std::atomic<float> m_threshold_db{-12.f};
        std::atomic<float> m_threshold_lin{0.251188643f}; // 10^(-12/20)
        std::atomic<float> m_ratio{4.f};
        std::atomic<float> m_makeup_db{0.f};
        std::atomic<float> m_attack_coef{0.f};
        std::atomic<float> m_release_coef{0.f};

        float m_envelope{0.f}; // audio-thread state
    };

} // namespace ambitap::dsp
