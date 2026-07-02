/// AmbiTap: target-independent ambisonics library
/// Binaural convolution core: the freestanding audio-path half of the
/// binaural renderer — per-channel SH-domain HRTF convolver bank + volume.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_BINAURAL_CORE_H
#define AMBITAP_DSP_BINAURAL_CORE_H

#include "../math/binaural/convolution.h"
#include "../math/core/indexing.h"
#include "../math/core/validate.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

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
    /// Internals are float32 throughout (partitioned_convolver32): on FPUs
    /// without hardware doubles (Cortex-M55, Hexagon) the double convolver
    /// is software floating point.
    ///
    /// Freestanding: builds with -fno-exceptions/-fno-rtti; no locks, no
    /// threads, no Eigen. prepare() allocates (init/control time);
    /// process() is wait-free and allocation-free.
    class binaural_core {
        size_t m_channels;
        size_t m_block_size {0};

        std::vector<partitioned_convolver32> m_left;
        std::vector<partitioned_convolver32> m_right;
        std::vector<float>                   m_temp;

        std::atomic<float> m_volume {1.0f};
        float              m_volume_current {1.0f}; // audio-thread ramp state

      public:
        /// @param order  Ambisonics order in [1, max_order].
        explicit binaural_core(int order)
            : m_channels(
                  channel_count(validated_order(order, 1, max_order, "dsp::binaural_core"))) {}

        size_t channels() const { return m_channels; }
        size_t block_size() const { return m_block_size; }
        bool   is_prepared() const { return !m_left.empty(); }

        /// (Re)build the convolver bank. Allocates — init/control time only.
        ///
        /// @param block_size  Processing block size: power of two, >= 4.
        /// @param left_firs   channels() pointers to SH-domain FIRs, left ear.
        /// @param right_firs  channels() pointers, right ear.
        /// @param taps        FIR length (same for every channel and ear),
        ///                    expressed at the host sample rate.
        /// @return false (leaving the core unprepared) on bad arguments.
        bool prepare(size_t block_size, const float* const* left_firs,
                     const float* const* right_firs, size_t taps) {
            m_left.clear();
            m_right.clear();
            m_block_size = 0;
            if (!left_firs || !right_firs || taps == 0 || block_size < 4
                || (block_size & (block_size - 1)) != 0) {
                return false;
            }
            for (size_t ch = 0; ch < m_channels; ++ch) {
                if (!left_firs[ch] || !right_firs[ch]) {
                    m_left.clear();
                    m_right.clear();
                    return false;
                }
                m_left.emplace_back(block_size, left_firs[ch], taps);
                m_right.emplace_back(block_size, right_firs[ch], taps);
            }
            m_temp.assign(block_size, 0.f);
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
        void process(const float* const* in, float* left, float* right,
                     size_t frame_count) noexcept {
            if (m_left.empty() || frame_count != m_block_size) {
                std::fill(left, left + frame_count, 0.f);
                std::fill(right, right + frame_count, 0.f);
                return;
            }

            std::fill(left, left + frame_count, 0.f);
            std::fill(right, right + frame_count, 0.f);

            for (size_t ch = 0; ch < m_channels; ++ch) {
                m_left[ch].process(in[ch], m_temp.data());
                for (size_t i = 0; i < frame_count; ++i) left[i] += m_temp[i];

                m_right[ch].process(in[ch], m_temp.data());
                for (size_t i = 0; i < frame_count; ++i) right[i] += m_temp[i];
            }

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
        void reset() {
            for (auto& c : m_left) c.reset();
            for (auto& c : m_right) c.reset();
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_BINAURAL_CORE_H
