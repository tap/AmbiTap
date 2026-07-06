/// @file binaural_core.h
/// @brief Binaural convolution core: the freestanding audio-path half of the
///        binaural renderer — per-channel SH-domain HRTF convolver bank + volume.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

#include "../math/binaural/convolver_bank.h"
#include "../math/core/indexing.h"
#include "../math/core/validate.h"

namespace ambitap::dsp {

    /// Convolve a HOA bus to binaural stereo against caller-supplied SH-domain
    /// HRTF FIRs and apply a click-free output volume.
    ///
    /// This is the embedded real-time profile's binaural engine and the
    /// engine inside dsp::binaural_renderer. It is deliberately dumb: the
    /// FIRs arrive as raw pointers, already selected (LS/MagLS/custom),
    /// already at the host sample rate, and already at the right order —
    /// dataset selection, resampling, validation, and head-tracking rotation
    /// live in the renderer (desktop) or on the host/control side (embedded;
    /// pair with dsp::matrix_applier for externally-computed rotation).
    ///
    /// Internals are float32 throughout (convolver_bank32): on FPUs without
    /// hardware doubles (Cortex-M55, Hexagon) the double convolver is
    /// software floating point. The bank shares input spectra across ears —
    /// channels() + 2 FFTs per block instead of a forward + inverse round
    /// trip per channel per ear.
    ///
    /// Freestanding: builds with -fno-exceptions/-fno-rtti; no locks, no
    /// threads, no Eigen. prepare() allocates (init/control time);
    /// process() is wait-free and allocation-free.
    class binaural_core {
        size_t m_channels;
        size_t m_block_size{0};

        convolver_bank32 m_bank;

        std::atomic<float> m_volume{1.0f};
        float              m_volume_current{1.0f}; // audio-thread ramp state

      public:
        /// @param order  Ambisonics order in [1, k_max_order].
        explicit binaural_core(int order)
            : m_channels(channel_count(validated_order(order, 1, k_max_order, "dsp::binaural_core"))) {}

        size_t channels() const { return m_channels; }
        size_t block_size() const { return m_block_size; }
        bool   is_prepared() const { return m_bank.is_prepared(); }

        /// (Re)build the convolver bank. Allocates — init/control time only.
        ///
        /// @param block_size  Processing block size: power of two, >= 4.
        /// @param left_firs   channels() pointers to SH-domain FIRs, left ear.
        /// @param right_firs  channels() pointers, right ear.
        /// @param taps        FIR length (same for every channel and ear),
        ///                    expressed at the host sample rate.
        /// @return false (leaving the core unprepared) on bad arguments.
        bool prepare(size_t block_size, const float* const* left_firs, const float* const* right_firs, size_t taps) {
            m_block_size = 0;
            if (!left_firs || !right_firs) {
                return false;
            }

            // Bank IR layout: row-major [ear][channel].
            std::vector<const float*> irs(2 * m_channels);
            for (size_t ch = 0; ch < m_channels; ++ch) {
                irs[ch]              = left_firs[ch];
                irs[m_channels + ch] = right_firs[ch];
            }
            if (!m_bank.prepare(block_size, m_channels, 2, irs.data(), taps)) {
                return false;
            }
            m_block_size = block_size;
            return true;
        }

        /// Linear output gain (post-convolution). RT-safe and race-free
        /// (atomic store); the audio thread ramps to it across one block.
        void  set_volume(float linear) { m_volume.store(linear, std::memory_order_relaxed); }
        float volume() const { return m_volume.load(std::memory_order_relaxed); }

        /// Render one block: in = channels() planar buffers of exactly
        /// block_size() frames; left/right = block_size() samples each.
        /// Emits silence until prepared (or on a block-size mismatch).
        /// Audio thread only; wait-free.
        void process(const float* const* in, float* left, float* right, size_t frame_count) noexcept {
            if (!m_bank.is_prepared() || frame_count != m_block_size) {
                std::fill(left, left + frame_count, 0.f);
                std::fill(right, right + frame_count, 0.f);
                return;
            }

            float* ears[2] = {left, right};
            m_bank.process(in, ears);

            // Volume: linear ramp from the previous value to the target
            // across this block (click-free, race-free).
            const float target = m_volume.load(std::memory_order_relaxed);
            if (target != 1.f || m_volume_current != target) {
                const float start = m_volume_current;
                const float step  = (target - start) / static_cast<float>(frame_count);
                float       g     = start;
                for (size_t i = 0; i < frame_count; ++i) {
                    g += step;
                    left[i] *= g;
                    right[i] *= g;
                }
                m_volume_current = target;
            }
        }

        /// Clear convolver input history; keep the FIRs and allocation.
        void reset() { m_bank.reset(); }
    };

} // namespace ambitap::dsp
