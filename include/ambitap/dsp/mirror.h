/// AmbiTap: target-independent ambisonics library
/// Soundfield mirroring (LR / FB / UD) via per-channel sign flips on the SH basis.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_MIRROR_H
#define AMBITAP_DSP_MIRROR_H

#include <array>
#include <cstddef>
#include <cstdlib>

#include "../math/core/indexing.h"
#include "../math/core/validate.h"
#include "util/smoothing.h"

namespace ambitap::dsp {

    /// Mirror a HOA signal across one or more cardinal planes: left-right,
    /// front-back, and/or up-down.
    ///
    /// Each mirror is a sign flip on the subset of SH channels whose basis
    /// function is odd along the corresponding axis:
    ///   LR (az -> -az):       channels with m < 0     (sin(|m|·az) is odd)
    ///   FB (az -> pi - az):   channels with m > 0 odd, m < 0 |m| even
    ///   UD (el -> -el):       channels with (n + |m|) odd  (Legendre parity)
    ///
    /// Combined mirrors compose by multiplying their signs (e.g. LR + FB).
    /// The audio path is one multiply per channel per sample.
    ///
    /// Threading: setters run on one control thread, the process methods on
    /// one audio thread. Sign changes crossfade over k_smoothing_samples
    /// (a sign flip is a full polarity crossfade — click-free); call
    /// snap_parameters() for offline/exact use. The audio path is wait-free.
    class mirror {
        int    m_order;
        size_t m_channels;
        bool   m_flip_lr{false};
        bool   m_flip_fb{false};
        bool   m_flip_ud{false};

        std::array<float, k_max_channel_count> m_sign{};

        smoothed_table<k_max_channel_count> m_smooth;

      public:
        /// @throws std::invalid_argument on out-of-range order.
        explicit mirror(int order)
            : m_order(validated_order(order, "dsp::mirror"))
            , m_channels(channel_count(order)) {
            recalculate();
            m_smooth.init(m_sign.data(), m_channels);
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void set_flip_lr(bool flip) {
            m_flip_lr = flip;
            recalculate();
            publish();
        }
        void set_flip_fb(bool flip) {
            m_flip_fb = flip;
            recalculate();
            publish();
        }
        void set_flip_ud(bool flip) {
            m_flip_ud = flip;
            recalculate();
            publish();
        }

        /// Skip the sign crossfade: the audio thread jumps straight to the
        /// latest signs on its next call. Offline rendering / tests.
        void snap_parameters() { m_smooth.snap(); }

        bool flip_lr() const { return m_flip_lr; }
        bool flip_fb() const { return m_flip_fb; }
        bool flip_ud() const { return m_flip_ud; }

        /// Per-channel sign: out[ch] = in[ch] * channel_sign(ch).
        float        channel_sign(size_t channel) const { return m_sign[channel]; }
        const float* signs() const { return m_sign.data(); }

        /// Mirror one frame of channels() samples. Output may alias input.
        /// Audio thread only; wait-free.
        void process_frame(const float* in, float* out) const noexcept {
            const float* s = m_smooth.tick(m_channels);
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in[ch] * s[ch];
            }
        }

        /// Mirror a block of planar channel buffers. Output may alias input.
        /// Audio thread only; wait-free.
        void process(const float* const* in, float* const* out, size_t frame_count) const noexcept {
            if (m_smooth.settled()) {
                const float* s = m_smooth.tick(m_channels);
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    const float  k   = s[ch];
                    const float* src = in[ch];
                    float*       dst = out[ch];
                    for (size_t i = 0; i < frame_count; ++i) {
                        dst[i] = src[i] * k;
                    }
                }
                return;
            }
            for (size_t i = 0; i < frame_count; ++i) {
                const float* s = m_smooth.tick(m_channels);
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    out[ch][i] = in[ch][i] * s[ch];
                }
            }
        }

      private:
        void recalculate() {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                const int m     = acn_degree(ch);
                const int n     = acn_order(ch);
                const int abs_m = std::abs(m);
                bool      flip  = false;

                if (m_flip_lr && m < 0) flip = !flip;
                if (m_flip_fb) {
                    const bool fb_flip = (m > 0 && (m & 1)) || (m < 0 && ((abs_m & 1) == 0));
                    if (fb_flip) flip = !flip;
                }
                if (m_flip_ud && (((n + abs_m) & 1) != 0)) flip = !flip;

                m_sign[ch] = flip ? -1.f : 1.f;
            }
        }

        void publish() { m_smooth.set(m_sign.data(), m_channels); }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_MIRROR_H
