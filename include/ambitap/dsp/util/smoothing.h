/// @file smoothing.h
/// @brief Race-free, click-free parameter smoothing between a control thread and
///        one audio thread.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tap::ambi::dsp {

    /// Number of samples over which parameter changes ramp to their target.
    /// ~2.7 ms at 48 kHz — long enough to suppress zipper noise and clicks,
    /// short enough to feel immediate.
    inline constexpr size_t k_smoothing_samples = 128;

    /// A fixed-capacity coefficient table with a control-thread setter and a
    /// linearly-ramped audio-thread reader.
    ///
    /// The control thread stores target values element-atomically (no torn
    /// floats, no locks); the audio thread owns the "current" values and ramps
    /// them to the target over k_smoothing_samples whenever the target
    /// generation changes. Wait-free on both sides.
    ///
    /// Threading contract: any number of control-thread set()/snap() calls
    /// (externally serialized, as with all processor setters); exactly ONE
    /// audio thread calling tick().
    template <size_t MaxN>
    class smoothed_table {
      public:
        /// Initialize target AND current values (constructor use: no fade-in).
        void init(const float* values, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                m_target[i].store(values[i], std::memory_order_relaxed);
                m_now[i] = m_to[i] = m_from[i] = values[i];
            }
            m_generation.store(m_generation.load(std::memory_order_relaxed) + 1, std::memory_order_release);
            m_seen = m_generation.load(std::memory_order_relaxed);
            m_pos  = k_smoothing_samples;
        }

        /// Store new target values; the audio thread ramps to them. Control thread.
        void set(const float* values, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                m_target[i].store(values[i], std::memory_order_relaxed);
            }
            m_generation.fetch_add(1, std::memory_order_release);
        }

        /// Make the audio thread jump straight to the current target instead
        /// of ramping (offline rendering / tests).
        void snap() {
            m_snap.store(true, std::memory_order_release);
            m_generation.fetch_add(1, std::memory_order_release);
        }

        /// Advance one frame and return the current (smoothed) values.
        /// Audio thread only; wait-free.
        const float* tick(size_t count) const noexcept {
            const auto gen = m_generation.load(std::memory_order_acquire);
            if (gen != m_seen) {
                m_seen = gen;
                for (size_t i = 0; i < count; ++i) {
                    m_from[i] = m_now[i];
                    m_to[i]   = m_target[i].load(std::memory_order_relaxed);
                }
                if (m_snap.exchange(false, std::memory_order_acq_rel)) {
                    for (size_t i = 0; i < count; ++i) {
                        m_now[i] = m_to[i];
                    }
                    m_pos = k_smoothing_samples;
                }
                else {
                    m_pos = 0;
                }
            }
            if (m_pos < k_smoothing_samples) {
                const float alpha = (static_cast<float>(m_pos) + 1.f) / static_cast<float>(k_smoothing_samples);
                for (size_t i = 0; i < count; ++i) {
                    m_now[i] = m_from[i] + (m_to[i] - m_from[i]) * alpha;
                }
                ++m_pos;
            }
            return m_now.data();
        }

        /// True when the current values have reached the target.
        bool settled() const noexcept {
            return m_pos >= k_smoothing_samples && m_generation.load(std::memory_order_acquire) == m_seen;
        }

      private:
        std::array<std::atomic<float>, MaxN> m_target{};
        std::atomic<std::uint32_t>           m_generation{0};
        mutable std::atomic<bool>            m_snap{false};

        // Audio-thread state (single reader).
        mutable std::array<float, MaxN> m_from{};
        mutable std::array<float, MaxN> m_to{};
        mutable std::array<float, MaxN> m_now{};
        mutable std::uint32_t           m_seen{0};
        mutable size_t                  m_pos{k_smoothing_samples};
    };

    /// Scalar variant of smoothed_table.
    class smoothed_scalar {
      public:
        explicit smoothed_scalar(float initial = 0.f) { init(initial); }

        void init(float value) {
            m_target.store(value, std::memory_order_relaxed);
            m_now = m_to = m_from = value;
            m_generation.fetch_add(1, std::memory_order_release);
            m_seen = m_generation.load(std::memory_order_relaxed);
            m_pos  = k_smoothing_samples;
        }

        void set(float value) {
            m_target.store(value, std::memory_order_relaxed);
            m_generation.fetch_add(1, std::memory_order_release);
        }

        /// Make the audio thread jump straight to the current target instead
        /// of ramping (offline rendering / tests).
        void snap() {
            m_snap.store(true, std::memory_order_release);
            m_generation.fetch_add(1, std::memory_order_release);
        }

        /// Target value as last set (control-thread getter).
        float target() const { return m_target.load(std::memory_order_relaxed); }

        /// Advance one frame and return the current (smoothed) value.
        /// Audio thread only; wait-free.
        float tick() const noexcept {
            const auto gen = m_generation.load(std::memory_order_acquire);
            if (gen != m_seen) {
                m_seen = gen;
                m_from = m_now;
                m_to   = m_target.load(std::memory_order_relaxed);
                if (m_snap.exchange(false, std::memory_order_acq_rel)) {
                    m_now = m_to;
                    m_pos = k_smoothing_samples;
                }
                else {
                    m_pos = 0;
                }
            }
            if (m_pos < k_smoothing_samples) {
                const float alpha = (static_cast<float>(m_pos) + 1.f) / static_cast<float>(k_smoothing_samples);
                m_now             = m_from + (m_to - m_from) * alpha;
                ++m_pos;
            }
            return m_now;
        }

        /// True when the current value has reached the target.
        bool settled() const noexcept {
            return m_pos >= k_smoothing_samples && m_generation.load(std::memory_order_acquire) == m_seen;
        }

      private:
        std::atomic<float>         m_target{0.f};
        std::atomic<std::uint32_t> m_generation{0};
        mutable std::atomic<bool>  m_snap{false};

        mutable float         m_from{0.f};
        mutable float         m_to{0.f};
        mutable float         m_now{0.f};
        mutable std::uint32_t m_seen{0};
        mutable size_t        m_pos{k_smoothing_samples};
    };

} // namespace tap::ambi::dsp
