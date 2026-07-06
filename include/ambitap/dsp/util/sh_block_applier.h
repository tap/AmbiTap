/// AmbiTap: target-independent ambisonics library
/// Block-diagonal SH matrix application — the rotation-shaped specialization
/// of matrix_applier.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_SH_BLOCK_APPLIER_H
#define AMBITAP_DSP_SH_BLOCK_APPLIER_H

#include <cstddef>

#include "matrix_applier.h"

namespace ambitap::dsp {

    /// Apply an SH rotation matrix to a HOA bus, with the same click-free
    /// crossfade semantics as matrix_applier — but visiting only the
    /// per-order diagonal blocks.
    ///
    /// SH rotation never mixes orders: the (C × C) matrix is block-diagonal
    /// with one (2l+1) × (2l+1) block per order l, and everything off-block
    /// is exactly zero. A dense multiply spends C² MACs per frame; this
    /// applier spends Σ(2l+1)² — at order 5 that is 286 instead of 1296
    /// (4.5×). Matrices still arrive in the dense row-major layout produced
    /// by compute_sh_rotation / dsp::rotator, so the two appliers are
    /// interchangeable for rotation-shaped matrices; results are identical
    /// (the skipped entries are exact zeros).
    ///
    /// Freestanding: no allocation, no exceptions, no locks. State is the
    /// fade progress, owned by the (single) audio thread.
    class sh_block_applier {
      public:
        /// Crossfade length applied when a new matrix is adopted.
        static constexpr size_t k_fade_samples = matrix_applier::k_fade_samples;

        /// Restart fade tracking; the next apply() fades in from `prev`.
        void reset() {
            m_fade_ref = nullptr;
            m_fade_pos = k_fade_samples;
        }

        /// Apply the block-diagonal `mat` (dense row-major C × C,
        /// C = (order+1)²) to frame_count frames of channel data.
        ///
        /// @param key    Identity of the matrix product: when it changes,
        ///               the crossfade restarts from `prev`.
        /// @param mat    Dense row-major (C × C); only diagonal blocks read.
        /// @param prev   Same shape/layout; the matrix faded from. Must stay
        ///               valid alongside mat during the fade.
        /// @param order  Ambisonics order; C = (order+1)².
        /// @param in     Input channel pointers (see frame_layout).
        /// @param out    Output channel pointers (see frame_layout).
        /// @param frame_count  Number of frames to process.
        /// @param frame_layout  true: single-frame channel arrays
        ///               (in[0][c], out[0][c]); false: planar buffers
        ///               (in[c][i], out[c][i]).
        void apply(const void* key, const float* mat, const float* prev, int order, const float* const* in,
                   float* const* out, size_t frame_count, bool frame_layout) noexcept {
            if (key != m_fade_ref) { // new matrix adopted: restart the crossfade
                m_fade_ref = key;
                m_fade_pos = 0;
            }

            const size_t C = static_cast<size_t>(order + 1) * static_cast<size_t>(order + 1);

            for (size_t i = 0; i < frame_count; ++i) {
                const bool  fading = m_fade_pos < k_fade_samples;
                const float alpha =
                    fading ? (static_cast<float>(m_fade_pos) + 1.f) / static_cast<float>(k_fade_samples) : 1.f;
                for (int l = 0; l <= order; ++l) {
                    const size_t base  = static_cast<size_t>(l) * static_cast<size_t>(l);
                    const size_t width = 2 * static_cast<size_t>(l) + 1;
                    for (size_t r = 0; r < width; ++r) {
                        const size_t row     = base + r;
                        float        acc_new = 0.f;
                        float        acc_old = 0.f;
                        for (size_t c = 0; c < width; ++c) {
                            const size_t col = base + c;
                            const float  x   = frame_layout ? in[0][col] : in[col][i];
                            acc_new += mat[row * C + col] * x;
                            if (fading) {
                                acc_old += prev[row * C + col] * x;
                            }
                        }
                        const float y = fading ? acc_old + (acc_new - acc_old) * alpha : acc_new;
                        if (frame_layout) {
                            out[0][row] = y;
                        }
                        else {
                            out[row][i] = y;
                        }
                    }
                }
                if (fading) {
                    ++m_fade_pos;
                }
            }
        }

      private:
        const void* m_fade_ref{nullptr};
        size_t      m_fade_pos{k_fade_samples};
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_SH_BLOCK_APPLIER_H
