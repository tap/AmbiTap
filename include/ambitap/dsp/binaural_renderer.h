/// AmbiTap: target-independent ambisonics library
/// Binaural renderer: HOA bus -> stereo via SH-domain HRTF convolution.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_BINAURAL_RENDERER_H
#define AMBITAP_DSP_BINAURAL_RENDERER_H

#include "../math/binaural/convolution.h"
#include "../math/binaural/hrtf_data.h"
#include "../math/binaural/ooura_fft.h"
#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "rotator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace ambitap::dsp {

    /// Decode a higher-order ambisonics signal to binaural stereo by convolving
    /// each HOA channel with its corresponding SH-domain HRTF filter and summing
    /// the per-ear results. Includes head-tracking via an embedded dsp::rotator
    /// (async SH-rotation rebuild; wait-free audio path).
    ///
    /// Built-in HRTF: MIT KEMAR (normal pinna), SN3D/ACN, projected to order 5 —
    /// so the order must be in [1, builtin_hrtf_order] unless a custom HRTF set
    /// is supplied via set_custom_hrtf().
    ///
    /// Lifecycle: construct with the order, call prepare() with the processing
    /// block size (power of two ≥ 4; rebuilds the partitioned convolvers). Until
    /// then process() emits silence. process() is real-time safe; set_projection,
    /// set_custom_hrtf, and prepare rebuild convolvers and are NOT — expect a
    /// short glitch when switching datasets live.
    class binaural_renderer {
      public:
        /// Built-in HRTF SH-projection selection:
        ///   ls    — standard least-squares projection (faithful low-frequency
        ///           reproduction; HF phase artifacts above ~3 kHz at order 5).
        ///   magls — magnitude-least-squares above the SH aliasing frequency;
        ///           better HF localization at the cost of LF timing precision.
        enum class hrtf_projection { ls, magls };

        /// Per-ear magnitude response at a probe direction (see probe_response).
        struct response {
            std::vector<float> frequencies; ///< Hz
            std::vector<float> left_db;     ///< dB, common-normalized (peak ear = 0 dB)
            std::vector<float> right_db;
        };

      private:
        int    m_order;
        size_t m_channels;
        size_t m_block_size {0};
        float  m_volume {1.0f};

        hrtf_projection m_projection {hrtf_projection::ls};

        // Custom (e.g. SOFA-loaded) SH-domain HRTFs; empty -> built-in dataset.
        std::vector<std::vector<float>> m_custom_left;
        std::vector<std::vector<float>> m_custom_right;

        std::vector<std::unique_ptr<partitioned_convolver>> m_conv_left;
        std::vector<std::unique_ptr<partitioned_convolver>> m_conv_right;
        std::vector<float>                                  m_temp;

        std::vector<std::vector<float>> m_rotated;     // planar rotation scratch
        std::vector<float*>             m_rotated_out; // pointers into m_rotated
        std::vector<const float*>       m_rotated_src;

        rotator m_head; // head-tracking; owns its worker thread

      public:
        /// @param order  Ambisonics order; [1, builtin_hrtf_order] for the
        ///               built-in dataset, higher only with set_custom_hrtf().
        explicit binaural_renderer(int order)
            : m_order(order)
            , m_channels(channel_count(order))
            , m_head(order) {}

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }
        size_t block_size() const { return m_block_size; }
        bool   is_prepared() const { return !m_conv_left.empty(); }

        /// Set the processing block size and (re)build the convolvers.
        /// @param block_size  Power of two, >= 4. Not RT-safe.
        void prepare(size_t block_size) {
            m_block_size = block_size;
            rebuild_convolvers();
        }

        /// Linear output gain (post-convolution). RT-safe.
        void  set_volume(float linear) { m_volume = linear; }
        float volume() const { return m_volume; }

        /// Select the built-in HRTF projection. Rebuilds convolvers when
        /// prepared (not RT-safe). Ignored while a custom HRTF set is loaded.
        void set_projection(hrtf_projection projection) {
            m_projection = projection;
            if (is_prepared()) rebuild_convolvers();
        }
        hrtf_projection projection() const { return m_projection; }

        /// Supply SH-domain HRTF FIRs (channels() filters per ear, equal length),
        /// e.g. loaded from a SOFA file. Rebuilds convolvers when prepared.
        void set_custom_hrtf(std::vector<std::vector<float>> left,
                             std::vector<std::vector<float>> right) {
            m_custom_left  = std::move(left);
            m_custom_right = std::move(right);
            if (is_prepared()) rebuild_convolvers();
        }

        /// Drop any custom HRTF set and return to the built-in dataset.
        void clear_custom_hrtf() {
            m_custom_left.clear();
            m_custom_right.clear();
            if (is_prepared()) rebuild_convolvers();
        }

        bool has_custom_hrtf() const { return !m_custom_left.empty(); }

        /// Head-tracking orientation (radians; yaw about +Z applied first, pitch
        /// about +Y second, roll about +X last). Submits an async matrix rebuild.
        void set_head_orientation(float yaw, float pitch, float roll) {
            m_head.set_rotation(yaw, pitch, roll);
        }
        void set_yaw(float radians) { m_head.set_yaw(radians); }
        void set_pitch(float radians) { m_head.set_pitch(radians); }
        void set_roll(float radians) { m_head.set_roll(radians); }

        /// Block until pending head-tracking rebuilds finish (tests/offline).
        void wait_for_settling() { m_head.wait_for_settling(); }

        /// SH-domain HRTF FIR for an ear (0 = left, 1 = right) and channel, plus
        /// its tap count: custom data when present, else built-in per projection().
        const float* hrtf_fir(int ear, size_t channel, size_t& length) const {
            if (has_custom_hrtf()) {
                length = m_custom_left[0].size();
                return (ear == 0 ? m_custom_left : m_custom_right)[channel].data();
            }
            length = builtin_hrtf_length;
            const bool magls = (m_projection == hrtf_projection::magls);
            if (ear == 0) {
                return magls ? builtin_hrtf_magls_left[channel] : builtin_hrtf_left[channel];
            }
            return magls ? builtin_hrtf_magls_right[channel] : builtin_hrtf_right[channel];
        }

        /// Render one block: in = channels() planar buffers of exactly
        /// block_size frames; left/right = block_size output samples each.
        /// Emits silence until prepare() has been called (or if frame_count
        /// doesn't match the prepared block size).
        void process(const float* const* in, float* left, float* right, size_t frame_count) {
            if (m_conv_left.empty() || frame_count != m_block_size) {
                std::fill(left, left + frame_count, 0.f);
                std::fill(right, right + frame_count, 0.f);
                return;
            }

            // Head-tracking: when no orientation has ever been set, feed the
            // input straight to the convolvers (no copy, bit-exact passthrough).
            const float* const* src = in;
            if (m_head.is_active()) {
                m_head.process(in, m_rotated_out.data(), frame_count);
                src = m_rotated_src.data();
            }

            std::fill(left, left + frame_count, 0.f);
            std::fill(right, right + frame_count, 0.f);

            for (size_t ch = 0; ch < m_channels; ++ch) {
                m_conv_left[ch]->process(src[ch], m_temp.data());
                for (size_t i = 0; i < frame_count; ++i) left[i] += m_temp[i];

                m_conv_right[ch]->process(src[ch], m_temp.data());
                for (size_t i = 0; i < frame_count; ++i) right[i] += m_temp[i];
            }

            if (m_volume != 1.f) {
                for (size_t i = 0; i < frame_count; ++i) {
                    left[i] *= m_volume;
                    right[i] *= m_volume;
                }
            }
        }

        /// Reconstruct the HRTF impulse response at a probe direction for each
        /// ear (sum of SH-domain FIRs weighted by the SH at that direction), FFT
        /// to a magnitude spectrum, and return per-ear dB traces. Magnitudes are
        /// normalized so the louder ear peaks at 0 dB, preserving the inter-aural
        /// level difference. Runs synchronously on the calling thread.
        ///
        /// @param sample_rate  Rate the HRTF taps are expressed at; defaults to
        ///                     the built-in dataset's rate.
        response probe_response(float azimuth, float elevation, size_t fft_size = 512,
                                float sample_rate = builtin_hrtf_sample_rate) const {
            float sh[max_channel_count];
            evaluate_sh(m_order, azimuth, elevation, sh);

            real_fft     fft(fft_size);
            const size_t bins = fft.num_bins();

            std::vector<float> mag[2];
            for (int ear = 0; ear < 2; ++ear) {
                std::vector<float> ir(fft_size, 0.f);
                size_t             len = 0;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    const float* fir = hrtf_fir(ear, ch, len);
                    const size_t n   = std::min(len, fft_size);
                    for (size_t t = 0; t < n; ++t) ir[t] += sh[ch] * fir[t];
                }
                std::vector<float> spec(fft_size);
                fft.forward(ir.data(), spec.data());

                // Ooura packing: spec[0]=Re(0), spec[1]=Re(N/2),
                // spec[2k]=Re(k), spec[2k+1]=Im(k).
                std::vector<float> m(bins);
                m[0]        = std::fabs(spec[0]);
                m[bins - 1] = std::fabs(spec[1]);
                for (size_t k = 1; k < bins - 1; ++k) {
                    const float re = spec[2 * k];
                    const float im = spec[2 * k + 1];
                    m[k]           = std::sqrt(re * re + im * im);
                }
                mag[ear] = std::move(m);
            }

            // Common normalization (preserve ILD): both ears referenced to the
            // overall peak -> 0 dB.
            float peak = 1e-9f;
            for (int ear = 0; ear < 2; ++ear)
                for (float v : mag[ear]) peak = std::max(peak, v);

            response r;
            r.frequencies.resize(bins);
            for (size_t k = 0; k < bins; ++k) {
                r.frequencies[k] = static_cast<float>(k) * sample_rate
                                   / static_cast<float>(fft_size);
            }
            r.left_db.resize(bins);
            r.right_db.resize(bins);
            for (size_t k = 0; k < bins; ++k) {
                r.left_db[k]  = 20.f * std::log10(std::max(mag[0][k] / peak, 1e-6f));
                r.right_db[k] = 20.f * std::log10(std::max(mag[1][k] / peak, 1e-6f));
            }
            return r;
        }

      private:
        void rebuild_convolvers() {
            m_conv_left.clear();
            m_conv_right.clear();
            if (m_block_size == 0) return;
            m_conv_left.reserve(m_channels);
            m_conv_right.reserve(m_channels);

            for (size_t ch = 0; ch < m_channels; ++ch) {
                size_t       len_l = 0, len_r = 0;
                const float* left  = hrtf_fir(0, ch, len_l);
                const float* right = hrtf_fir(1, ch, len_r);
                m_conv_left.emplace_back(
                    std::make_unique<partitioned_convolver>(m_block_size, left, len_l));
                m_conv_right.emplace_back(
                    std::make_unique<partitioned_convolver>(m_block_size, right, len_r));
            }
            m_temp.assign(m_block_size, 0.f);

            m_rotated.assign(m_channels, std::vector<float>(m_block_size, 0.f));
            m_rotated_out.clear();
            m_rotated_src.clear();
            for (auto& buf : m_rotated) {
                m_rotated_out.push_back(buf.data());
                m_rotated_src.push_back(buf.data());
            }
        }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_BINAURAL_RENDERER_H
