/// AmbiTap: target-independent ambisonics library
/// Soundfield mirroring (LR / FB / UD) via per-channel sign flips on the SH basis.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_MIRROR_H
#define AMBITAP_DSP_MIRROR_H

#include "../math/core/indexing.h"

#include <array>
#include <cstddef>
#include <cstdlib>

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
    class mirror {
        int    m_order;
        size_t m_channels;
        bool   m_flip_lr {false};
        bool   m_flip_fb {false};
        bool   m_flip_ud {false};

        std::array<float, max_channel_count> m_sign {};

      public:
        explicit mirror(int order)
            : m_order(order)
            , m_channels(channel_count(order)) {
            recalculate();
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        void set_flip_lr(bool flip) { m_flip_lr = flip; recalculate(); }
        void set_flip_fb(bool flip) { m_flip_fb = flip; recalculate(); }
        void set_flip_ud(bool flip) { m_flip_ud = flip; recalculate(); }

        bool flip_lr() const { return m_flip_lr; }
        bool flip_fb() const { return m_flip_fb; }
        bool flip_ud() const { return m_flip_ud; }

        /// Per-channel sign: out[ch] = in[ch] * channel_sign(ch).
        float        channel_sign(size_t channel) const { return m_sign[channel]; }
        const float* signs() const { return m_sign.data(); }

        /// Mirror one frame of channels() samples. Output may alias input.
        void process_frame(const float* in, float* out) const {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                out[ch] = in[ch] * m_sign[ch];
            }
        }

        /// Mirror a block of planar channel buffers. Output may alias input.
        void process(const float* const* in, float* const* out, size_t frame_count) const {
            for (size_t ch = 0; ch < m_channels; ++ch) {
                const float  s   = m_sign[ch];
                const float* src = in[ch];
                float*       dst = out[ch];
                for (size_t i = 0; i < frame_count; ++i) {
                    dst[i] = src[i] * s;
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
                    const bool fb_flip =
                        (m > 0 && (m & 1)) || (m < 0 && ((abs_m & 1) == 0));
                    if (fb_flip) flip = !flip;
                }
                if (m_flip_ud && (((n + abs_m) & 1) != 0)) flip = !flip;

                m_sign[ch] = flip ? -1.f : 1.f;
            }
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_MIRROR_H
