/// AmbiTap: target-independent ambisonics library
/// Real-time partitioned overlap-save convolver using Ooura FFT.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_CONVOLUTION_H
#define AMBITAP_MATH_CONVOLUTION_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

#include "ooura_fft.h"

namespace ambitap {

    /// Partitioned overlap-save FIR convolver.
    ///
    /// Convolves a continuous stream of fixed-size input blocks with a pre-loaded
    /// impulse response. The IR is partitioned into segments of size m_block_size
    /// and each segment is pre-transformed to the frequency domain at construction.
    /// Per-block compute is one forward FFT, num_partitions complex-MACs in the
    /// frequency domain, and one inverse FFT.
    ///
    /// The block size must be a power of 2 (>= 4) and is fixed at construction.
    /// IR length is arbitrary.
    ///
    /// Memory layout: all frequency-domain data uses the Ooura packing convention
    /// from ooura_fft.h.
    ///
    /// Instantiated at two precisions (the I/O is float either way):
    ///   partitioned_convolver   — double internals; the desktop default.
    ///   partitioned_convolver32 — float internals (real_fft32); the embedded
    ///                             real-time profile, for FPUs without hardware
    ///                             doubles (e.g. Cortex-M55, Hexagon).
    template <typename Real, typename Fft>
    class basic_partitioned_convolver {
        size_t                         m_block_size;
        size_t                         m_fft_size; // 2 * m_block_size
        Fft                            m_fft;
        size_t                         m_num_partitions{0};
        std::vector<std::vector<Real>> m_ir_freq;    // [partition][fft_size]
        std::vector<std::vector<Real>> m_input_freq; // [partition][fft_size] ring buffer
        std::vector<Real>              m_accum;      // [fft_size] freq-domain sum
        std::vector<Real>              m_input_buf;  // [fft_size] time-domain [prev|curr]
        std::vector<Real>              m_output_buf; // [fft_size] inverse-FFT scratch

        size_t m_ring_pos{0};

      public:
        /// Construct with an IR.
        explicit basic_partitioned_convolver(size_t block_size, const float* ir = nullptr, size_t ir_length = 0)
            : m_block_size(block_size)
            , m_fft_size(block_size * 2)
            , m_fft(m_fft_size) {
            assert(block_size >= 4 && (block_size & (block_size - 1)) == 0);
            if (ir != nullptr && ir_length > 0) {
                {
                    set_ir(ir, ir_length);
                }
            }
        }

        size_t block_size() const { return m_block_size; }
        size_t num_partitions() const { return m_num_partitions; }

        /// Load a new impulse response. Resets state.
        void set_ir(const float* ir, size_t ir_length) {
            if (ir_length == 0) {
                m_num_partitions = 0;
                return;
            }

            m_num_partitions = (ir_length + m_block_size - 1) / m_block_size;

            m_ir_freq.resize(m_num_partitions);
            std::vector<Real> temp(m_fft_size, Real(0));
            for (size_t p = 0; p < m_num_partitions; ++p) {
                std::fill(temp.begin(), temp.end(), Real(0));
                const size_t offset = p * m_block_size;
                const size_t len    = std::min(m_block_size, ir_length - offset);
                for (size_t i = 0; i < len; ++i) {
                    temp[i] = static_cast<Real>(ir[offset + i]);
                }
                m_fft.forward_inplace(temp.data());
                m_ir_freq[p].assign(temp.begin(), temp.end());
            }

            m_input_freq.assign(m_num_partitions, std::vector<Real>(m_fft_size, Real(0)));
            m_accum.assign(m_fft_size, Real(0));
            m_input_buf.assign(m_fft_size, Real(0));
            m_output_buf.assign(m_fft_size, Real(0));
            m_ring_pos = 0;
        }

        /// Process one block. input and output are m_block_size samples each.
        /// Overwrites output; does not accumulate. Real-time safe after set_ir().
        void process(const float* input, float* output) {
            if (m_num_partitions == 0) {
                std::memset(output, 0, m_block_size * sizeof(float));
                return;
            }

            // Slide [prev | curr] into the FFT buffer.
            for (size_t i = 0; i < m_block_size; ++i) {
                m_input_buf[i] = m_input_buf[m_block_size + i];
            }
            for (size_t i = 0; i < m_block_size; ++i) {
                m_input_buf[m_block_size + i] = static_cast<Real>(input[i]);
            }

            // Forward FFT into the current ring slot.
            auto& current_input_freq = m_input_freq[m_ring_pos];
            std::copy(m_input_buf.begin(), m_input_buf.end(), current_input_freq.begin());
            m_fft.forward_inplace(current_input_freq.data());

            // Frequency-domain MAC across partitions (Ooura packing).
            std::fill(m_accum.begin(), m_accum.end(), Real(0));
            for (size_t p = 0; p < m_num_partitions; ++p) {
                const size_t input_idx = (m_ring_pos + m_num_partitions - p) % m_num_partitions;
                const auto&  X         = m_input_freq[input_idx];
                const auto&  H         = m_ir_freq[p];

                m_accum[0] += X[0] * H[0]; // DC bin
                m_accum[1] += X[1] * H[1]; // Nyquist bin
                for (size_t k = 1; k < m_fft_size / 2; ++k) {
                    const Real xr = X[2 * k];
                    const Real xi = X[2 * k + 1];
                    const Real hr = H[2 * k];
                    const Real hi = H[2 * k + 1];
                    m_accum[2 * k] += xr * hr - xi * hi;
                    m_accum[2 * k + 1] += xr * hi + xi * hr;
                }
            }

            std::copy(m_accum.begin(), m_accum.end(), m_output_buf.begin());
            m_fft.inverse_inplace(m_output_buf.data());
            const Real scale = Real(2) / static_cast<Real>(m_fft_size);
            // Overlap-save: the valid output is the second half.
            for (size_t i = 0; i < m_block_size; ++i) {
                output[i] = static_cast<float>(m_output_buf[m_block_size + i] * scale);
            }

            m_ring_pos = (m_ring_pos + 1) % m_num_partitions;
        }

        /// Reset input history; keep the loaded IR.
        void reset() {
            for (auto& buf : m_input_freq) {
                {
                    std::fill(buf.begin(), buf.end(), Real(0));
                }
            }
            std::fill(m_input_buf.begin(), m_input_buf.end(), Real(0));
            m_ring_pos = 0;
        }
    };

    using partitioned_convolver   = basic_partitioned_convolver<double, real_fft>;
    using partitioned_convolver32 = basic_partitioned_convolver<float, real_fft32>;

} // namespace ambitap

#endif // AMBITAP_MATH_CONVOLUTION_H
