/// @file convolver_bank.h
/// @brief Shared-spectrum partitioned convolver bank: many inputs, few outputs,
///        one forward FFT per input and one inverse FFT per output.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

#include "ooura_fft.h"

namespace ambitap {

    /// Partitioned overlap-save convolution of N input channels against
    /// N × M impulse responses, mixed down to M outputs — the shape of
    /// SH-domain binaural rendering (N HOA channels, M = 2 ears).
    ///
    /// A bank of independent partitioned_convolvers computes one forward and
    /// one inverse FFT per (input, output) pair — 4·N transforms per block
    /// for M = 2 — even though every convolver of an input transforms the
    /// SAME signal. This class shares those spectra: per block it computes
    /// N forward FFTs (one per input), accumulates everything in the
    /// frequency domain, and runs M inverse FFTs (one per output). Same
    /// math, same partition scheme, same Ooura packing as
    /// partitioned_convolver — just without the redundant transforms.
    /// N = 36, M = 2 (order-5 binaural): 38 FFTs per block instead of 144.
    ///
    /// The output equals a per-pair convolver arrangement summed per output,
    /// up to floating-point accumulation order.
    ///
    /// Freestanding: builds with -fno-exceptions/-fno-rtti; no locks, no
    /// threads. prepare() allocates (init/control time); process() is
    /// wait-free and allocation-free.
    template <typename Real, typename Fft>
    class basic_convolver_bank {
        size_t m_block_size{0};
        size_t m_fft_size{0}; // 2 * m_block_size
        size_t m_inputs{0};
        size_t m_outputs{0};
        size_t m_partitions{0};
        size_t m_ring_pos{0};

        std::vector<Fft>  m_fft;        // exactly one; vector for deferred init
        std::vector<Real> m_ir_freq;    // [output][input][partition][fft_size]
        std::vector<Real> m_input_freq; // [input][partition][fft_size] rings
        std::vector<Real> m_input_buf;  // [input][fft_size] time [prev|curr]
        std::vector<Real> m_accum;      // [fft_size]
        std::vector<Real> m_output_buf; // [fft_size]

        Real* input_buf(size_t in) { return m_input_buf.data() + in * m_fft_size; }
        Real* input_freq(size_t in, size_t slot) {
            return m_input_freq.data() + (in * m_partitions + slot) * m_fft_size;
        }
        const Real* ir_freq(size_t out, size_t in, size_t p) const {
            return m_ir_freq.data() + (((out * m_inputs) + in) * m_partitions + p) * m_fft_size;
        }

      public:
        basic_convolver_bank() = default;

        size_t block_size() const { return m_block_size; }
        size_t inputs() const { return m_inputs; }
        size_t outputs() const { return m_outputs; }
        size_t num_partitions() const { return m_partitions; }
        bool   is_prepared() const { return m_partitions != 0; }

        /// (Re)build the bank. Allocates — init/control time only.
        ///
        /// @param block_size  Power of two, >= 4.
        /// @param num_inputs  N input channels.
        /// @param num_outputs M output channels.
        /// @param irs         num_outputs * num_inputs FIR pointers, row-major
        ///                    [output][input], each `taps` samples long.
        /// @param taps        Common FIR length, >= 1.
        /// @return false (leaving the bank unprepared) on bad arguments.
        bool prepare(size_t block_size, size_t num_inputs, size_t num_outputs, const float* const* irs, size_t taps) {
            m_partitions = 0;
            if (!irs || taps == 0 || num_inputs == 0 || num_outputs == 0 || block_size < 4
                || (block_size & (block_size - 1)) != 0) {
                return false;
            }
            for (size_t i = 0; i < num_inputs * num_outputs; ++i) {
                if (!irs[i]) {
                    return false;
                }
            }

            m_block_size = block_size;
            m_fft_size   = block_size * 2;
            m_inputs     = num_inputs;
            m_outputs    = num_outputs;
            m_fft.clear();
            m_fft.emplace_back(m_fft_size);

            const size_t partitions = (taps + block_size - 1) / block_size;

            m_ir_freq.assign(num_outputs * num_inputs * partitions * m_fft_size, Real(0));
            std::vector<Real> temp(m_fft_size);
            for (size_t out = 0; out < num_outputs; ++out) {
                for (size_t in = 0; in < num_inputs; ++in) {
                    const float* ir = irs[out * num_inputs + in];
                    for (size_t p = 0; p < partitions; ++p) {
                        std::fill(temp.begin(), temp.end(), Real(0));
                        const size_t offset = p * block_size;
                        const size_t len    = std::min(block_size, taps - offset);
                        for (size_t i = 0; i < len; ++i) {
                            temp[i] = static_cast<Real>(ir[offset + i]);
                        }
                        m_fft[0].forward_inplace(temp.data());
                        std::copy(temp.begin(), temp.end(),
                                  m_ir_freq.begin()
                                      + static_cast<long>(((out * num_inputs + in) * partitions + p) * m_fft_size));
                    }
                }
            }

            m_input_freq.assign(num_inputs * partitions * m_fft_size, Real(0));
            m_input_buf.assign(num_inputs * m_fft_size, Real(0));
            m_accum.assign(m_fft_size, Real(0));
            m_output_buf.assign(m_fft_size, Real(0));
            m_ring_pos   = 0;
            m_partitions = partitions;
            return true;
        }

        /// Process one block: in = inputs() pointers, out = outputs()
        /// pointers, block_size() samples each. Overwrites out; does not
        /// accumulate. Real-time safe after prepare().
        void process(const float* const* in, float* const* out) noexcept {
            if (m_partitions == 0) {
                for (size_t o = 0; o < m_outputs; ++o) {
                    std::memset(out[o], 0, m_block_size * sizeof(float));
                }
                return;
            }

            // One forward FFT per input into its ring slot.
            for (size_t c = 0; c < m_inputs; ++c) {
                Real* buf = input_buf(c);
                for (size_t i = 0; i < m_block_size; ++i) {
                    buf[i] = buf[m_block_size + i];
                }
                for (size_t i = 0; i < m_block_size; ++i) {
                    buf[m_block_size + i] = static_cast<Real>(in[c][i]);
                }
                Real* slot = input_freq(c, m_ring_pos);
                std::copy(buf, buf + m_fft_size, slot);
                m_fft[0].forward_inplace(slot);
            }

            // Per output: accumulate every (input, partition) product in the
            // frequency domain (Ooura packing), then one inverse FFT.
            for (size_t o = 0; o < m_outputs; ++o) {
                std::fill(m_accum.begin(), m_accum.end(), Real(0));
                for (size_t c = 0; c < m_inputs; ++c) {
                    for (size_t p = 0; p < m_partitions; ++p) {
                        const size_t slot = (m_ring_pos + m_partitions - p) % m_partitions;
                        const Real*  X    = input_freq(c, slot);
                        const Real*  H    = ir_freq(o, c, p);

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
                }

                std::copy(m_accum.begin(), m_accum.end(), m_output_buf.begin());
                m_fft[0].inverse_inplace(m_output_buf.data());
                const Real scale = Real(2) / static_cast<Real>(m_fft_size);
                // Overlap-save: the valid output is the second half.
                for (size_t i = 0; i < m_block_size; ++i) {
                    out[o][i] = static_cast<float>(m_output_buf[m_block_size + i] * scale);
                }
            }

            m_ring_pos = (m_ring_pos + 1) % m_partitions;
        }

        /// Reset input history; keep the loaded IRs.
        void reset() {
            std::fill(m_input_freq.begin(), m_input_freq.end(), Real(0));
            std::fill(m_input_buf.begin(), m_input_buf.end(), Real(0));
            m_ring_pos = 0;
        }
    };

    using convolver_bank   = basic_convolver_bank<double, real_fft>;
    using convolver_bank32 = basic_convolver_bank<float, real_fft32>;

} // namespace ambitap
