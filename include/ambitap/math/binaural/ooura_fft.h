/// @file ooura_fft.h
/// @brief Modern C++ wrapper around the Ooura split-radix real FFT.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

// Ooura C functions (from third_party/ooura/fftsg.c). They are built into
// the AmbiTap::fft static library; link that target (see CMakeLists.txt for
// how the vendored fftsg.c is compiled).
extern "C" {
void rdft(int n, int isgn, double* a, int* ip, double* w);
void cdft(int n, int isgn, double* a, int* ip, double* w);
// Single-precision instantiation (third_party/ooura/fftsg_float.c), for the
// embedded real-time profile where doubles are software floats.
void rdft_f(int n, int isgn, float* a, int* ip, float* w);
void cdft_f(int n, int isgn, float* a, int* ip, float* w);
}

namespace ambitap {

    /// Real FFT using the Ooura split-radix algorithm.
    ///
    /// Wraps the Ooura `rdft` function with automatic workspace management. FFT
    /// size must be a power of 2, fixed at construction. Workspace (bit-reversal
    /// and trig tables) is allocated once and reused.
    ///
    /// Output packing after a forward transform of N real samples:
    ///   - bin[0].real    = out[0]   (DC)
    ///   - bin[N/2].real  = out[1]   (Nyquist)
    ///   - bin[k].real    = out[2k], bin[k].imag = out[2k+1]  for 1 <= k < N/2
    /// The number of complex bins is N/2 + 1.
    class real_fft {
      public:
        explicit real_fft(size_t size)
            : m_size(static_cast<int>(size))
            , m_buf(size)
            , m_ip(2 + static_cast<size_t>(std::sqrt(static_cast<double>(size) / 2.0)) + 1)
            , m_w(size / 2) {
            assert(size >= 4 && (size & (size - 1)) == 0);
            m_ip[0] = 0; // triggers Ooura table init on first call
        }

        size_t size() const { return static_cast<size_t>(m_size); }
        size_t num_bins() const { return static_cast<size_t>(m_size / 2 + 1); }

        /// Forward real FFT: time-domain float[size] -> packed frequency-domain float[size].
        /// Output may alias input.
        void forward(const float* input, float* output) {
            for (int i = 0; i < m_size; ++i) {
                {
                    m_buf[static_cast<size_t>(i)] = input[i];
                }
            }
            rdft(m_size, 1, m_buf.data(), m_ip.data(), m_w.data());
            for (int i = 0; i < m_size; ++i) {
                {
                    output[i] = static_cast<float>(m_buf[static_cast<size_t>(i)]);
                }
            }
        }

        /// Inverse real FFT: packed frequency-domain float[size] -> time-domain float[size].
        /// Output is scaled by 2/size to produce a normalized inverse. May alias input.
        void inverse(const float* input, float* output) {
            for (int i = 0; i < m_size; ++i) {
                {
                    m_buf[static_cast<size_t>(i)] = input[i];
                }
            }
            rdft(m_size, -1, m_buf.data(), m_ip.data(), m_w.data());
            const double scale = 2.0 / static_cast<double>(m_size);
            for (int i = 0; i < m_size; ++i) {
                {
                    output[i] = static_cast<float>(m_buf[static_cast<size_t>(i)] * scale);
                }
            }
        }

        /// In-place forward FFT on a double buffer (no float conversion).
        void forward_inplace(double* data) { rdft(m_size, 1, data, m_ip.data(), m_w.data()); }

        /// In-place inverse FFT on a double buffer (no float conversion, no scaling).
        /// Caller must scale by 2/size for a normalized inverse.
        void inverse_inplace(double* data) { rdft(m_size, -1, data, m_ip.data(), m_w.data()); }

      private:
        int                 m_size;
        std::vector<double> m_buf;
        std::vector<int>    m_ip;
        std::vector<double> m_w;
    };

    /// Single-precision real FFT — the same Ooura algorithm instantiated for
    /// float (fftsg_float.c). This is the embedded real-time profile's FFT:
    /// on FPUs without hardware doubles (e.g. Cortex-M55) the double path
    /// above falls back to software floating point. Packing convention and
    /// scaling match real_fft exactly.
    class real_fft32 {
      public:
        explicit real_fft32(size_t size)
            : m_size(static_cast<int>(size))
            , m_ip(2 + static_cast<size_t>(std::sqrt(static_cast<double>(size) / 2.0)) + 1)
            , m_w(size / 2) {
            assert(size >= 4 && (size & (size - 1)) == 0);
            m_ip[0] = 0; // triggers Ooura table init on first call
        }

        size_t size() const { return static_cast<size_t>(m_size); }
        size_t num_bins() const { return static_cast<size_t>(m_size / 2 + 1); }

        /// In-place forward FFT on a float buffer.
        void forward_inplace(float* data) { rdft_f(m_size, 1, data, m_ip.data(), m_w.data()); }

        /// In-place inverse FFT on a float buffer (no scaling; caller scales
        /// by 2/size for a normalized inverse).
        void inverse_inplace(float* data) { rdft_f(m_size, -1, data, m_ip.data(), m_w.data()); }

      private:
        int                m_size;
        std::vector<int>   m_ip;
        std::vector<float> m_w;
    };

} // namespace ambitap
