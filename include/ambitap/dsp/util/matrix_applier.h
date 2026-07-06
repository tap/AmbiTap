/// @file matrix_applier.h
/// @brief Crossfading matrix application — the audio-thread half of the rotator and
///        decoder, and the embedded profile's entry point for precomputed matrices.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <cstddef>

namespace ambitap::dsp {

    /// Apply a row-major (rows × cols) matrix to a multichannel signal, with
    /// click-free adoption: each newly presented matrix crossfades in over
    /// k_fade_samples against its companion `prev` matrix.
    ///
    /// This is the freestanding core shared by dsp::rotator and dsp::decoder
    /// (which build their matrices on a worker thread and publish them
    /// wait-free). On embedded targets without threads — or without Eigen
    /// for matrix construction — drive it directly: compute or precompute
    /// the matrix on the control side and pass it to apply(); a change of
    /// matrix identity (the `key` pointer) restarts the fade.
    ///
    /// Freestanding: no allocation, no exceptions, no locks; `<cstddef>` only.
    /// State is the fade progress, owned by the (single) audio thread.
    class matrix_applier {
      public:
        /// Crossfade length applied when a new matrix is adopted.
        static constexpr size_t k_fade_samples = 256;

        /// Restart fade tracking (e.g. after an audio dropout); the next
        /// apply() fades its matrix in from `prev` again.
        void reset() {
            m_fade_ref = nullptr;
            m_fade_pos = k_fade_samples;
        }

        /// Apply `mat` (row-major rows × cols) to frame_count frames.
        ///
        /// @param key    Identity of the matrix product: when it changes, the
        ///               crossfade restarts from `prev`. Pass the address of
        ///               the published product (or of the matrix itself).
        /// @param mat    Row-major (rows × cols) matrix.
        /// @param prev   Same shape; the matrix faded from. Must stay valid
        ///               alongside mat for the k_fade_samples after adoption.
        /// @param rows   Output channel count (matrix rows).
        /// @param cols   Input channel count (matrix columns).
        /// @param in     Input channel pointers (see frame_layout).
        /// @param out    Output channel pointers (see frame_layout).
        /// @param frame_count  Number of frames to process.
        /// @param frame_layout  true: in/out are single-frame channel arrays
        ///               (in[0][c], out[0][r]); false: planar buffers
        ///               (in[c][i], out[r][i]).
        void apply(const void* key, const float* mat, const float* prev, size_t rows, size_t cols,
                   const float* const* in, float* const* out, size_t frame_count, bool frame_layout) noexcept {
            if (key != m_fade_ref) { // new matrix adopted: restart the crossfade
                m_fade_ref = key;
                m_fade_pos = 0;
            }

            for (size_t i = 0; i < frame_count; ++i) {
                const bool  fading = m_fade_pos < k_fade_samples;
                const float alpha =
                    fading ? (static_cast<float>(m_fade_pos) + 1.f) / static_cast<float>(k_fade_samples) : 1.f;
                for (size_t r = 0; r < rows; ++r) {
                    float acc_new = 0.f;
                    float acc_old = 0.f;
                    for (size_t c = 0; c < cols; ++c) {
                        const float x = frame_layout ? in[0][c] : in[c][i];
                        acc_new += mat[r * cols + c] * x;
                        if (fading) {
                            acc_old += prev[r * cols + c] * x;
                        }
                    }
                    const float y = fading ? acc_old + (acc_new - acc_old) * alpha : acc_new;
                    if (frame_layout) {
                        out[0][r] = y;
                    }
                    else {
                        out[r][i] = y;
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
