/// AmbiTap: target-independent ambisonics library
/// Binaural renderer: HOA bus -> stereo via SH-domain HRTF convolution.
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_DSP_BINAURAL_RENDERER_H
#define AMBITAP_DSP_BINAURAL_RENDERER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include "../math/binaural/hrtf_data.h"
#include "../math/binaural/ooura_fft.h"
#include "../math/binaural/resample.h"
#include "../math/core/indexing.h"
#include "../math/core/spherical_harmonics.h"
#include "../math/core/validate.h"
#include "binaural_core.h"
#include "rotator.h"

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
    /// Sample rate: the built-in dataset is sampled at 44.1 kHz
    /// (builtin_hrtf_sample_rate). Pass the host rate to prepare() and the
    /// FIRs are resampled to match at build time; without it, running at any
    /// other rate shifts every spectral cue and shrinks ITDs. Custom HRTFs
    /// are used as-is and must already be at the host rate.
    ///
    /// Lifecycle: construct with the order, call prepare() with the processing
    /// block size (power of two ≥ 4; rebuilds the partitioned convolvers) and
    /// the host sample rate. Until then process() emits silence. process() is
    /// real-time safe; set_projection, set_custom_hrtf, and prepare rebuild
    /// convolvers and are NOT — stop audio (or accept undefined behavior, not
    /// merely a glitch) when switching datasets live.
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
        size_t m_block_size{0};
        float  m_sample_rate{builtin_hrtf_sample_rate};

        hrtf_projection m_projection{hrtf_projection::ls};

        // Custom (e.g. SOFA-loaded) SH-domain HRTFs; empty -> built-in dataset.
        std::vector<std::vector<float>> m_custom_left;
        std::vector<std::vector<float>> m_custom_right;

        // Convolver bank + volume ramp (the freestanding audio-path engine).
        binaural_core m_core;

        std::vector<std::vector<float>> m_rotated;     // planar rotation scratch
        std::vector<float*>             m_rotated_out; // pointers into m_rotated
        std::vector<const float*>       m_rotated_src;

        // Head orientation as last set; the scene rotator below is driven with
        // the INVERSE of this so sources stay fixed in the world frame.
        float m_head_yaw{0.0f};
        float m_head_pitch{0.0f};
        float m_head_roll{0.0f};

        rotator m_head; // head-tracking (counter-rotation); owns its worker thread

      public:
        /// @param order  Ambisonics order in [1, max_order]. Orders above
        ///               builtin_hrtf_order require set_custom_hrtf() before
        ///               prepare(); prepare() throws otherwise.
        /// @throws std::invalid_argument on out-of-range order.
        explicit binaural_renderer(int order)
            : m_order(validated_order(order, 1, max_order, "dsp::binaural_renderer"))
            , m_channels(channel_count(order))
            , m_core(order)
            , m_head(order) {}

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }
        size_t block_size() const { return m_block_size; }
        bool   is_prepared() const { return m_core.is_prepared(); }

        /// Set the processing block size and host sample rate, then (re)build
        /// the convolvers. The built-in HRTFs are resampled from 44.1 kHz to
        /// sample_rate; custom HRTFs are used as-is.
        /// @param block_size   Power of two, >= 4. Not RT-safe.
        /// @param sample_rate  Host rate in Hz; defaults to the built-in
        ///                     dataset's native 44.1 kHz.
        /// @throws std::invalid_argument when order() > builtin_hrtf_order and
        ///         no custom HRTF set has been supplied.
        void prepare(size_t block_size, float sample_rate = builtin_hrtf_sample_rate) {
            m_block_size  = block_size;
            m_sample_rate = sample_rate;
            rebuild_convolvers();
        }

        /// Host sample rate as last passed to prepare().
        float sample_rate() const { return m_sample_rate; }

        /// Linear output gain (post-convolution). RT-safe and race-free
        /// (atomic store); the audio thread ramps to it across one block.
        void  set_volume(float linear) { m_core.set_volume(linear); }
        float volume() const { return m_core.volume(); }

        /// Select the built-in HRTF projection. Rebuilds convolvers when
        /// prepared (not RT-safe). Ignored while a custom HRTF set is loaded.
        void set_projection(hrtf_projection projection) {
            m_projection = projection;
            if (is_prepared()) rebuild_convolvers();
        }
        hrtf_projection projection() const { return m_projection; }

        /// Supply SH-domain HRTF FIRs (channels() filters per ear, equal length),
        /// e.g. loaded from a SOFA file. Rebuilds convolvers when prepared.
        /// @throws std::invalid_argument when either ear does not have exactly
        ///         channels() FIRs, any FIR is empty, or lengths are unequal.
        void set_custom_hrtf(std::vector<std::vector<float>> left, std::vector<std::vector<float>> right) {
            if (left.size() != m_channels || right.size() != m_channels) {
                throw std::invalid_argument("ambitap::dsp::binaural_renderer::set_custom_hrtf: need exactly "
                                            "channels() FIRs per ear");
            }
            const size_t taps = left.front().size();
            for (const auto* ear : {&left, &right}) {
                for (const auto& fir : *ear) {
                    if (fir.empty() || fir.size() != taps) {
                        throw std::invalid_argument("ambitap::dsp::binaural_renderer::set_custom_hrtf: all FIRs "
                                                    "must be non-empty and equal length");
                    }
                }
            }
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
        /// about +Y second, roll about +X last — the same convention as
        /// math/core/rotation.h). The scene is counter-rotated by the INVERSE of
        /// this orientation so sources stay fixed in the world frame: turn your
        /// head left and a front source moves to your right ear. Submits an
        /// async matrix rebuild.
        void set_head_orientation(float yaw, float pitch, float roll) {
            m_head_yaw   = yaw;
            m_head_pitch = pitch;
            m_head_roll  = roll;
            apply_head_orientation();
        }
        void set_yaw(float radians) {
            m_head_yaw = radians;
            apply_head_orientation();
        }
        void set_pitch(float radians) {
            m_head_pitch = radians;
            apply_head_orientation();
        }
        void set_roll(float radians) {
            m_head_roll = radians;
            apply_head_orientation();
        }

        /// Block until pending head-tracking rebuilds finish (tests/offline).
        void wait_for_settling() { m_head.wait_for_settling(); }

        /// SH-domain HRTF FIR for an ear (0 = left, 1 = right) and channel, plus
        /// its tap count: custom data when present, else built-in per projection().
        const float* hrtf_fir(int ear, size_t channel, size_t& length) const {
            if (has_custom_hrtf()) {
                const auto& set = (ear == 0 ? m_custom_left : m_custom_right);
                length          = set[channel].size();
                return set[channel].data();
            }
            length           = builtin_hrtf_length;
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
        void process(const float* const* in, float* left, float* right, size_t frame_count) noexcept {
            if (!m_core.is_prepared() || frame_count != m_block_size) {
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

            m_core.process(src, left, right, frame_count);
        }

        /// Reconstruct the HRTF impulse response at a probe direction for each
        /// ear (sum of SH-domain FIRs weighted by the SH at that direction), FFT
        /// to a magnitude spectrum, and return per-ear dB traces. Magnitudes are
        /// normalized so the louder ear peaks at 0 dB, preserving the inter-aural
        /// level difference. Runs synchronously on the calling thread.
        ///
        /// @param azimuth      Probe azimuth in radians (0 = front, +π/2 = left).
        /// @param elevation    Probe elevation in radians (0 = horizon, +π/2 = zenith).
        /// @param fft_size     FFT length for the magnitude spectrum.
        /// @param sample_rate  Rate the HRTF taps are expressed at; defaults to
        ///                     the built-in dataset's rate.
        response probe_response(float azimuth, float elevation, size_t fft_size = 512,
                                float sample_rate = builtin_hrtf_sample_rate) const {
            require_hrtf_coverage("probe_response");
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
                    for (size_t t = 0; t < n; ++t)
                        ir[t] += sh[ch] * fir[t];
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
                for (float v : mag[ear])
                    peak = std::max(peak, v);

            response r;
            r.frequencies.resize(bins);
            for (size_t k = 0; k < bins; ++k) {
                r.frequencies[k] = static_cast<float>(k) * sample_rate / static_cast<float>(fft_size);
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
        /// The built-in dataset only covers builtin_hrtf_order; reading past it
        /// would index off the end of the embedded tables.
        void require_hrtf_coverage(const char* who) const {
            if (!has_custom_hrtf() && m_order > builtin_hrtf_order) {
                throw std::invalid_argument(std::string("ambitap::dsp::binaural_renderer::") + who + ": order "
                                            + std::to_string(m_order) + " exceeds the built-in HRTF order "
                                            + std::to_string(builtin_hrtf_order) + "; supply set_custom_hrtf() first");
            }
        }

        /// Drive the scene rotator with the inverse of the head orientation.
        /// The inverse of R = Rz(yaw)*Ry(pitch)*Rx(roll) is its transpose; the
        /// equivalent Z-Y-X Euler angles are extracted from that so the rotator
        /// (which composes in the same order) reproduces it exactly.
        void apply_head_orientation() {
            const Eigen::Matrix3f R = (Eigen::AngleAxisf(m_head_yaw, Eigen::Vector3f::UnitZ())
                                       * Eigen::AngleAxisf(m_head_pitch, Eigen::Vector3f::UnitY())
                                       * Eigen::AngleAxisf(m_head_roll, Eigen::Vector3f::UnitX()))
                                          .toRotationMatrix();
            const Eigen::Matrix3f inv = R.transpose();

            const float yaw   = std::atan2(inv(1, 0), inv(0, 0));
            const float pitch = std::asin(std::clamp(-inv(2, 0), -1.0f, 1.0f));
            const float roll  = std::atan2(inv(2, 1), inv(2, 2));
            m_head.set_rotation(yaw, pitch, roll);
        }

        void rebuild_convolvers() {
            require_hrtf_coverage("prepare");
            if (m_block_size == 0) return;

            // Built-in FIRs are stored at 44.1 kHz; adapt them to the host
            // rate here (control thread; allocation is fine). Custom FIRs are
            // the caller's responsibility and pass through untouched.
            const bool resample = !has_custom_hrtf() && m_sample_rate != builtin_hrtf_sample_rate;

            std::vector<std::vector<float>> resampled_l, resampled_r;
            std::vector<const float*>       left_ptrs(m_channels), right_ptrs(m_channels);
            size_t                          taps = 0;
            for (size_t ch = 0; ch < m_channels; ++ch) {
                size_t       len_l = 0, len_r = 0;
                const float* left  = hrtf_fir(0, ch, len_l);
                const float* right = hrtf_fir(1, ch, len_r);
                if (resample) {
                    resampled_l.push_back(resample_fir(left, len_l, builtin_hrtf_sample_rate, m_sample_rate));
                    resampled_r.push_back(resample_fir(right, len_r, builtin_hrtf_sample_rate, m_sample_rate));
                    left_ptrs[ch]  = resampled_l.back().data();
                    right_ptrs[ch] = resampled_r.back().data();
                    taps           = resampled_l.back().size();
                }
                else {
                    left_ptrs[ch]  = left;
                    right_ptrs[ch] = right;
                    taps           = len_l;
                }
            }
            m_core.prepare(m_block_size, left_ptrs.data(), right_ptrs.data(), taps);

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
