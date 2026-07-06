/// @file xtc.h
/// @brief Transaural / crosstalk cancellation (XTC): stereo or binaural program ->
///        two loudspeaker feeds for a known symmetric speaker geometry, via a
///        regularized frequency-domain inverse of the KEMAR 2x2 plant.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "../math/binaural/convolution.h"
#include "../math/binaural/hrtf_data.h"
#include "../math/binaural/ooura_fft.h"
#include "../math/binaural/resample.h"
#include "../math/core/spherical_harmonics.h"

namespace ambitap::dsp {

    /// Crosstalk canceller for playing binaural (or plain stereo) program over
    /// two loudspeakers: each ear should hear only its own channel, so the
    /// filter matrix H(f) approximately inverts the acoustic plant C(f) — the
    /// 2x2 matrix of head-related transfer functions from each speaker to each
    /// ear. v1 is computed-plant only: C is reconstructed from the built-in
    /// order-5 KEMAR SH set (LS projection — the design needs the phase, which
    /// magLS gives up) at azimuths ±span/2, elevation 0. Measured/in-room
    /// plants are out of scope (docs/PERCEPTUAL-VERIFICATION.md, open q. 2).
    ///
    /// Filter design (control thread, double precision, deterministic):
    ///
    ///   H(f) = (C^H C + beta(f) I)^-1 C^H
    ///
    /// with frequency-DEPENDENT Tikhonov regularization beta(f): small inside
    /// the 300 Hz – 6 kHz cancellation band, ramped large (raised cosine in
    /// log f) outside it. Below ~300 Hz the plant is nearly singular (the
    /// inter-ear path difference is a tiny fraction of a wavelength) and above
    /// ~6 kHz cancellation is a fiction that evaporates with millimeters of
    /// head motion, so the filter deliberately gives up there. Two per-bin
    /// post-steps bound coloration and gain — the canonical failure of
    /// frequency-INdependent regularization is dB-scale ipsilateral combing
    /// (Choueiri), and these are what the X4/X5 verification gates measure:
    ///
    ///   - Ipsilateral normalization: each bin is scaled so the diagonal of
    ///     the performance matrix P = C·H sits at 0 dB (geometric mean of the
    ///     two ears, smoothed by ±1/6 octave so the filter never chases a
    ///     plant notch narrower than the coloration gate can hear — fully
    ///     inverting KEMAR's raw ~8.5 kHz pinna notch would demand ~25 dB of
    ///     over-fit gain). Plain Tikhonov rolloff would fold
    ///     out-of-band content to the -6 dB mono sum; the normalization
    ///     instead relaxes H toward a unity-diagonal passthrough, keeping the
    ///     1/3-octave |P_ii| flat across the band edges (X4). Outside
    ///     100 Hz – 16 kHz the factor is frozen at the nearest in-range bin
    ///     so a dead plant bin (DC) cannot demand infinite gain.
    ///   - Gain ceiling: the whole design is then scaled by one global factor
    ///     so the largest normalized |H_ij| across the coloration-audit range
    ///     (k_fit_lo_hz – k_fit_hi_hz, the X4 band plus its smoothing window)
    ///     lands on the +12 dB ceiling (k_gain_ceiling_db, less a small
    ///     margin for the realized FIR). A GLOBAL scale preserves both the
    ///     rejection ratios and the coloration flatness, trading only
    ///     absolute level — the
    ///     KEMAR plant is ~14 dB weaker at 300 Hz than at its 3 kHz
    ///     resonance, so pinning in-band |P_ii| to exactly 0 dB would demand
    ///     ~17 dB at the low band edge; the global fit spends the overage as
    ///     uniform attenuation instead of per-bin sag (which would gut both
    ///     X1 and X4 at the edge). Any residual out-of-fit-band bin over the
    ///     ceiling (deep plant notches) is capped individually.
    ///
    /// The four filters are realized as k_fir_length-tap FIRs: inverse FFT of
    /// the design grid, circularly rotated by k_fir_length/2 samples (the
    /// modeling delay that makes the intrinsically non-causal inverse causal),
    /// then edge-tapered (Tukey-style raised-cosine ends). Processing latency
    /// is therefore latency_samples() = k_fir_length/2 samples on top of the
    /// host's block. After windowing, the realized worst-case gain is
    /// re-measured and a matching static makeup attenuation is baked into the
    /// shipped FIRs (X5): max_f max_ij |H_ij(f)| <= 1, so a full-scale input
    /// cannot clip a single filter path (the coherent sum of the two paths
    /// into one speaker can still exceed unity on pathological program; the
    /// ceiling is the budget, not a hard limiter).
    ///
    /// Distance: v1 keeps the geometry symmetric (one span, one distance), so
    /// per-speaker propagation compensation — path delay and 1/r gain — is
    /// identical for both speakers and factors out of the design as a common
    /// scalar; the filters are far-field-plant invariant to it. The distance
    /// is still real geometry: the verification gates (and any future
    /// near-field or asymmetric plant) displace the head against it.
    ///
    /// The per-bin math is a closed-form 2x2 complex inversion rather than
    /// Eigen: (C^H C + beta I) is Hermitian 2x2, whose inverse is three real
    /// operations and a conjugate — and keeping Eigen out of this header lets
    /// hosts (e.g. the Max externals) build it against AmbiTap::fft alone.
    ///
    /// Determinism (gate X6): the design is single-threaded IEEE double
    /// arithmetic with no seeds and no environment dependence — identical
    /// parameters yield byte-identical filters.
    ///
    /// Lifecycle: construct (designs immediately at the default 44.1 kHz, so
    /// fir()/makeup_gain() are valid before audio exists), then prepare() with
    /// the block size and host rate to build the partitioned convolvers.
    /// process() is wait-free and allocation-free; it emits silence until
    /// prepared. Setters redesign the filters and rebuild the convolvers and
    /// are NOT RT-safe — call them with audio stopped, or wrap this class in
    /// an async publication scheme (the Max wrapper crossfades convolver pairs
    /// built on the control thread; see ambitap.xtc~).
    class xtc {
      public:
        /// Taps per FIR filter. The modeling delay (= reported latency) is
        /// half of this.
        static constexpr size_t k_fir_length = 1024;
        /// Design/analysis FFT grid (4x the FIR length: the windowed FIR's
        /// response is smooth at this resolution, so the realized-gain scan
        /// cannot miss an inter-bin peak).
        static constexpr size_t k_design_grid = 4096;
        /// Cancellation band: where beta(f) is small and rejection is bought.
        static constexpr float k_band_low_hz  = 300.0f;
        static constexpr float k_band_high_hz = 6000.0f;
        /// Verification gate X5: no |H_ij(f)| may exceed this.
        static constexpr float k_gain_ceiling_db = 12.0f;

        static constexpr float k_min_span_deg = 5.0f;
        static constexpr float k_max_span_deg = 120.0f;
        static constexpr float k_min_distance = 0.1f;

      private:
        // Regularization profile: beta ramps (raised cosine in log f) from
        // beta_out down to beta_in across [lo_stop, lo_pass] and back up
        // across [hi_pass, hi_stop]. The ramps sit OUTSIDE the cancellation
        // band so the 300 Hz / 6 kHz edge bins already enjoy full inversion
        // (the ~fs/k_fir_length spectral smoothing of the windowed FIR bleeds
        // the transition inward; these margins absorb that).
        static constexpr double k_reg_lo_stop_hz = 100.0;
        static constexpr double k_reg_lo_pass_hz = 250.0;
        static constexpr double k_reg_hi_pass_hz = 6200.0;
        static constexpr double k_reg_hi_stop_hz = 9500.0;
        /// In-band beta at regularization() = 0.5, relative to the mean
        /// in-band plant power; the attribute scans ±1 decade around it.
        static constexpr double k_beta_in_default = 1.0e-3;
        /// Out-of-band beta (relative). Large: outside the band the inverse
        /// collapses toward the normalized matched filter.
        static constexpr double k_beta_out = 1.0;
        /// Ipsilateral normalization is frozen outside this range (the plant
        /// has no usable energy at DC / the top octave; boosting it to 0 dB
        /// would burn the whole gain budget on inaudible bins).
        static constexpr double k_norm_lo_hz = 100.0;
        static constexpr double k_norm_hi_hz = 16000.0;
        /// The global gain fit scales the design so the worst normalized
        /// |H_ij| inside this range sits on the ceiling. The range is the X4
        /// coloration band (200 Hz – 8 kHz) widened by the 1/3-octave
        /// smoothing half-window: nothing the coloration gate can see is
        /// per-bin capped, so P_ii stays flat wherever it is measured. Wide
        /// spans pay for this — KEMAR's ipsilateral pinna notch at ~7.4 kHz
        /// is 15+ dB deep at ±30°, and holding X4 flatness through it eats
        /// most of the ceiling as static level.
        static constexpr double k_fit_lo_hz = 178.0;
        static constexpr double k_fit_hi_hz = 8980.0;
        /// Design-time cap margin under k_gain_ceiling_db: windowing the FIR
        /// can overshoot the design grid slightly; the margin keeps the
        /// REALIZED response under the gate.
        static constexpr double k_cap_margin_db = 0.5;
        /// The built-in KEMAR data lives at 44.1 kHz; when the host rate is
        /// higher, the resampled plant above the dataset's usable edge is
        /// anti-alias rolloff, not acoustics — inverting it blows the gain
        /// budget on junk. The response fades to zero across this band.
        static constexpr double k_out_fade_lo_hz = 19000.0;
        static constexpr double k_out_fade_hi_hz = 21000.0;
        /// Raised-cosine taper length at each FIR end.
        static constexpr size_t k_window_taper = 128;

        float  m_span_deg{20.0f};
        float  m_distance{1.0f};
        float  m_regularization{0.5f};
        float  m_sample_rate{builtin_hrtf_sample_rate};
        size_t m_block_size{0};

        // Shipped FIRs (makeup attenuation baked in), indexed [speaker][input]
        // with 0 = left. Speaker-left feed = fir(0,0)*in_L + fir(0,1)*in_R.
        std::vector<float> m_fir[2][2];
        float              m_makeup{1.0f};         // linear, <= 1
        float              m_design_gain_db{0.0f}; // realized max |H_ij| pre-makeup

        // Audio-path engine: convolvers in [LL, LR, RL, RR] order + mix
        // scratch (so outputs may alias inputs).
        std::vector<partitioned_convolver> m_conv;
        std::vector<float>                 m_mix_left;
        std::vector<float>                 m_mix_right;
        std::vector<float>                 m_scratch;

      public:
        /// Designs the default geometry (span 20°, distance 1 m,
        /// regularization 0.5) at 44.1 kHz immediately; call prepare() before
        /// audio.
        xtc() { redesign(); }

        /// Full angle between the loudspeakers in degrees (speakers sit at
        /// ±span/2 azimuth), clamped to [k_min_span_deg, k_max_span_deg].
        /// Redesigns; NOT RT-safe.
        void set_span(float degrees) {
            m_span_deg = std::clamp(degrees, k_min_span_deg, k_max_span_deg);
            redesign();
        }
        float span() const { return m_span_deg; }

        /// Listener-to-speaker distance in meters (>= k_min_distance). Stated
        /// geometry; see the class comment for why the symmetric far-field
        /// design is invariant to it. Redesigns; NOT RT-safe.
        void set_distance(float meters) {
            m_distance = std::max(meters, k_min_distance);
            redesign();
        }
        float distance() const { return m_distance; }

        /// In-band regularization amount in [0, 1]: scales beta inside the
        /// cancellation band across ±1 decade (0.5 = the verified default).
        /// Lower buys deeper rejection at the price of a hotter, more
        /// position-sensitive filter. Redesigns; NOT RT-safe.
        void set_regularization(float amount) {
            m_regularization = std::clamp(amount, 0.0f, 1.0f);
            redesign();
        }
        float regularization() const { return m_regularization; }

        /// Redesign at a new host rate without touching the convolvers (for
        /// hosts that own their convolution engines). NOT RT-safe.
        void set_sample_rate(float sample_rate) {
            m_sample_rate = sample_rate;
            redesign();
        }
        float sample_rate() const { return m_sample_rate; }

        /// Build the audio-path convolvers for the processing block size and
        /// host sample rate. Block size must be a power of two >= 4. NOT
        /// RT-safe.
        void prepare(size_t block_size, float sample_rate = builtin_hrtf_sample_rate) {
            m_block_size  = block_size;
            m_sample_rate = sample_rate;
            redesign();
        }
        bool   is_prepared() const { return !m_conv.empty(); }
        size_t block_size() const { return m_block_size; }

        /// Modeling delay of the designed filters, in samples at sample_rate().
        static constexpr size_t latency_samples() { return k_fir_length / 2; }

        /// Shipped FIR (makeup attenuation baked in) feeding `speaker` (0 =
        /// left, 1 = right) from program `input` (0 = left, 1 = right).
        const std::vector<float>& fir(size_t speaker, size_t input) const { return m_fir[speaker][input]; }

        /// Static makeup attenuation baked into the shipped FIRs (linear,
        /// <= 1): the reciprocal of the realized worst-case filter gain.
        float makeup_gain() const { return m_makeup; }

        /// Realized worst-case filter gain BEFORE makeup, in dB — the value
        /// gate X5 bounds by k_gain_ceiling_db.
        float design_gain_db() const { return m_design_gain_db; }

        /// Clear convolver history; keep filters and allocations.
        void reset() {
            for (auto& c : m_conv) {
                c.reset();
            }
        }

        /// Process one block: two program inputs -> two speaker feeds, each
        /// frame_count samples. Outputs may alias inputs. Emits silence until
        /// prepare() has been called (or if frame_count doesn't match the
        /// prepared block size). Audio thread; wait-free.
        void process(const float* in_left, const float* in_right, float* out_left, float* out_right,
                     size_t frame_count) noexcept {
            if (m_conv.empty() || frame_count != m_block_size) {
                std::fill(out_left, out_left + frame_count, 0.0f);
                std::fill(out_right, out_right + frame_count, 0.0f);
                return;
            }
            m_conv[0].process(in_left, m_mix_left.data());
            m_conv[1].process(in_right, m_scratch.data());
            for (size_t i = 0; i < frame_count; ++i) {
                m_mix_left[i] += m_scratch[i];
            }
            m_conv[2].process(in_left, m_mix_right.data());
            m_conv[3].process(in_right, m_scratch.data());
            for (size_t i = 0; i < frame_count; ++i) {
                m_mix_right[i] += m_scratch[i];
            }
            std::copy_n(m_mix_left.begin(), frame_count, out_left);
            std::copy_n(m_mix_right.begin(), frame_count, out_right);
        }

      private:
        using cd = std::complex<double>;

        /// KEMAR HRIR at (azimuth, elevation 0) for one ear, reconstructed
        /// from the built-in LS SH set (the probe_response inner loop).
        static std::vector<float> kemar_hrir(float azimuth, int ear) {
            float sh[k_max_channel_count];
            evaluate_sh(builtin_hrtf_order, azimuth, 0.0f, sh);
            std::vector<float> ir(builtin_hrtf_length, 0.0f);
            for (size_t ch = 0; ch < builtin_hrtf_channels; ++ch) {
                const float* fir = (ear == 0) ? builtin_hrtf_left[ch] : builtin_hrtf_right[ch];
                for (size_t t = 0; t < builtin_hrtf_length; ++t) {
                    ir[t] += sh[ch] * fir[t];
                }
            }
            return ir;
        }

        /// beta(f) relative to the mean in-band plant power: log-cosine blend
        /// between beta_in (inside the cancellation band) and k_beta_out.
        static double beta_profile(double f, double beta_in) {
            double w = 0.0; // 0 = in band, 1 = out of band
            if (f <= k_reg_lo_stop_hz || f >= k_reg_hi_stop_hz) {
                w = 1.0;
            }
            else if (f < k_reg_lo_pass_hz) {
                const double t = std::log(f / k_reg_lo_stop_hz) / std::log(k_reg_lo_pass_hz / k_reg_lo_stop_hz);
                w              = 0.5 * (1.0 + std::cos(3.14159265358979323846 * t));
            }
            else if (f > k_reg_hi_pass_hz) {
                const double t = std::log(f / k_reg_hi_pass_hz) / std::log(k_reg_hi_stop_hz / k_reg_hi_pass_hz);
                w              = 0.5 * (1.0 - std::cos(3.14159265358979323846 * t));
            }
            return std::exp((1.0 - w) * std::log(beta_in) + w * std::log(k_beta_out));
        }

        /// Full design pass: plant -> regularized inverse -> normalization ->
        /// cap -> FIR realization -> makeup -> convolvers. Control thread;
        /// allocates.
        void redesign() {
            constexpr size_t G             = k_design_grid;
            constexpr size_t bins          = G / 2 + 1;
            const double     fs            = static_cast<double>(m_sample_rate);
            const float      half_span_rad = 0.5f * m_span_deg * 0.017453292519943295f; // pi/180

            // ---- Plant spectra: speaker s -> ear e, at ±span/2 --------------
            // (Symmetric geometry: the common propagation delay and 1/r gain
            // of the two equal-length paths cancel in the inverse and are
            // omitted; see the class comment.)
            real_fft            fft(G);
            std::vector<double> spec[2][2]; // [ear][speaker], Ooura packing
            for (int e = 0; e < 2; ++e) {
                for (int s = 0; s < 2; ++s) {
                    const float        az   = (s == 0) ? half_span_rad : -half_span_rad;
                    std::vector<float> hrir = kemar_hrir(az, e);
                    if (m_sample_rate != builtin_hrtf_sample_rate) {
                        hrir = resample_fir(hrir.data(), hrir.size(), builtin_hrtf_sample_rate, m_sample_rate);
                    }
                    auto&        buf = spec[e][s];
                    const size_t n   = std::min(hrir.size(), G);
                    buf.assign(G, 0.0);
                    for (size_t t = 0; t < n; ++t) {
                        buf[t] = static_cast<double>(hrir[t]);
                    }
                    fft.forward_inplace(buf.data());
                }
            }
            const auto plant_bin = [&](int e, int s, size_t k) -> cd {
                const auto& b = spec[e][s];
                if (k == 0) {
                    return {b[0], 0.0};
                }
                if (k == G / 2) {
                    return {b[1], 0.0};
                }
                return {b[2 * k], b[2 * k + 1]};
            };

            // ±1/6-octave power smoothing. Bin frequency is proportional to
            // the bin index, so the window is an index range.
            const auto smooth_third_octave = [](std::vector<double>& x) {
                const size_t        n = x.size();
                std::vector<double> out(n, 0.0);
                for (size_t k = 1; k < n; ++k) {
                    const auto lo =
                        std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(k) * 0.8908987)));
                    const auto hi =
                        std::min<size_t>(n - 1, static_cast<size_t>(std::floor(static_cast<double>(k) * 1.1224620)));
                    double acc = 0.0;
                    for (size_t j = lo; j <= hi; ++j) {
                        acc += x[j];
                    }
                    out[k] = acc / static_cast<double>(hi - lo + 1);
                }
                out[0] = out[1];
                x      = std::move(out);
            };

            // ---- Local plant power (1/3-octave smoothed): the reference
            //      beta scales by. Per-bin, not a global mean — the KEMAR
            //      plant is ~14 dB weaker at 300 Hz than at its 3 kHz
            //      resonance, and a globally scaled beta would swamp the
            //      weak-plant bins and forfeit cancellation exactly at the
            //      band edge the X1 minimum gate watches. ---------------------
            std::vector<double> plant_power(bins, 0.0);
            for (size_t k = 0; k < bins; ++k) {
                double p = 0.0;
                for (int e = 0; e < 2; ++e) {
                    for (int s = 0; s < 2; ++s) {
                        p += std::norm(plant_bin(e, s, k));
                    }
                }
                plant_power[k] = 0.5 * p;
            }
            smooth_third_octave(plant_power);

            // ---- Per-bin regularized inverse + raw diagonal level -----------
            const double beta_in =
                k_beta_in_default * std::pow(10.0, 2.0 * (static_cast<double>(m_regularization) - 0.5));
            std::vector<cd>     h[2][2];
            std::vector<double> diag(bins, 0.0); // sqrt(|P00|*|P11|), pre-norm
            for (auto& row : h) {
                for (auto& v : row) {
                    v.assign(bins, cd{0.0, 0.0});
                }
            }

            for (size_t k = 0; k < bins; ++k) {
                const double f    = static_cast<double>(k) * fs / static_cast<double>(G);
                const double beta = beta_profile(f, beta_in) * plant_power[k];

                const cd c00 = plant_bin(0, 0, k), c01 = plant_bin(0, 1, k);
                const cd c10 = plant_bin(1, 0, k), c11 = plant_bin(1, 1, k);

                // A = C^H C + beta I (Hermitian), inverted in closed form.
                const double a00 = std::norm(c00) + std::norm(c10) + beta;
                const double a11 = std::norm(c01) + std::norm(c11) + beta;
                const cd     a01 = std::conj(c00) * c01 + std::conj(c10) * c11;
                const double det = a00 * a11 - std::norm(a01);

                // H = A^-1 C^H, rows = speakers, columns = program inputs.
                const cd h00 = (a11 * std::conj(c00) - a01 * std::conj(c01)) / det;
                const cd h01 = (a11 * std::conj(c10) - a01 * std::conj(c11)) / det;
                const cd h10 = (a00 * std::conj(c01) - std::conj(a01) * std::conj(c00)) / det;
                const cd h11 = (a00 * std::conj(c11) - std::conj(a01) * std::conj(c10)) / det;

                // Ipsilateral level of P = C·H at this bin (geometric mean of
                // the two ears) — the normalization divisor.
                const cd p00 = c00 * h00 + c01 * h10;
                const cd p11 = c10 * h01 + c11 * h11;
                diag[k]      = std::sqrt(std::abs(p00) * std::abs(p11));

                h[0][0][k] = h00;
                h[0][1][k] = h01;
                h[1][0][k] = h10;
                h[1][1][k] = h11;
            }

            // ---- Normalize the diagonal to 0 dB: ±1/6-octave power-smoothed
            //      (never chase a narrow plant notch), frozen outside the
            //      normalization range ---------------------------------------
            std::vector<double> smooth(bins, 0.0);
            for (size_t k = 0; k < bins; ++k) {
                smooth[k] = diag[k] * diag[k];
            }
            smooth_third_octave(smooth);
            for (size_t k = 0; k < bins; ++k) {
                smooth[k] = std::sqrt(smooth[k]);
            }

            size_t k_lo = bins - 1, k_hi = 0;
            for (size_t k = 0; k < bins; ++k) {
                const double f = static_cast<double>(k) * fs / static_cast<double>(G);
                if (f >= k_norm_lo_hz && k < k_lo) {
                    k_lo = k;
                }
                if (f <= k_norm_hi_hz && k > k_hi) {
                    k_hi = k;
                }
            }
            if (k_hi < k_lo) {
                k_lo = k_hi; // degenerate rates: freeze everywhere
            }
            const double cap = std::pow(10.0, (static_cast<double>(k_gain_ceiling_db) - k_cap_margin_db) / 20.0);

            // Per-bin normalization factor and its gain demand; global fit of
            // the worst in-fit-band demand onto the ceiling.
            std::vector<double> norm(bins, 0.0), demand(bins, 0.0);
            double              fit_max = 0.0;
            for (size_t k = 0; k < bins; ++k) {
                const double d = smooth[std::clamp(k, k_lo, k_hi)];
                norm[k]        = (d > 1e-12) ? 1.0 / d : 0.0;
                for (auto& row : h) {
                    for (auto& v : row) {
                        demand[k] = std::max(demand[k], std::abs(v[k]) * norm[k]);
                    }
                }
                const double f = static_cast<double>(k) * fs / static_cast<double>(G);
                if (f >= k_fit_lo_hz && f <= k_fit_hi_hz) {
                    fit_max = std::max(fit_max, demand[k]);
                }
            }
            const double fit = (fit_max > cap) ? cap / fit_max : 1.0;
            for (size_t k = 0; k < bins; ++k) {
                double n = norm[k] * fit;
                if (demand[k] * fit > cap) {
                    n *= cap / (demand[k] * fit); // out-of-band stragglers
                }
                const double f = static_cast<double>(k) * fs / static_cast<double>(G);
                if (f >= k_out_fade_hi_hz) {
                    n = 0.0;
                }
                else if (f > k_out_fade_lo_hz) {
                    const double t = (f - k_out_fade_lo_hz) / (k_out_fade_hi_hz - k_out_fade_lo_hz);
                    n *= 0.5 * (1.0 + std::cos(3.14159265358979323846 * t));
                }
                for (auto& row : h) {
                    for (auto& v : row) {
                        v[k] *= n;
                    }
                }
            }

            // ---- Realize the FIRs: IFFT, modeling-delay rotation, taper -----
            constexpr size_t    delay = k_fir_length / 2;
            std::vector<double> time(G);
            for (int s = 0; s < 2; ++s) {
                for (int i = 0; i < 2; ++i) {
                    auto& hb = h[s][i];
                    time[0]  = hb[0].real();
                    time[1]  = hb[G / 2].real();
                    for (size_t k = 1; k < G / 2; ++k) {
                        time[2 * k]     = hb[k].real();
                        time[2 * k + 1] = hb[k].imag();
                    }
                    fft.inverse_inplace(time.data());
                    const double scale = 2.0 / static_cast<double>(G);

                    auto& fir = m_fir[s][i];
                    fir.assign(k_fir_length, 0.0f);
                    for (size_t t = 0; t < k_fir_length; ++t) {
                        const size_t src = (t + G - delay) % G;
                        double       w   = 1.0;
                        if (t < k_window_taper) {
                            w = 0.5
                                * (1.0
                                   - std::cos(3.14159265358979323846 * (static_cast<double>(t) + 0.5)
                                              / static_cast<double>(k_window_taper)));
                        }
                        else if (t >= k_fir_length - k_window_taper) {
                            const size_t r = k_fir_length - 1 - t;
                            w              = 0.5
                                * (1.0
                                   - std::cos(3.14159265358979323846 * (static_cast<double>(r) + 0.5)
                                              / static_cast<double>(k_window_taper)));
                        }
                        fir[t] = static_cast<float>(time[src] * scale * w);
                    }
                }
            }

            // ---- Realized worst-case gain -> matching makeup attenuation ---
            double gmax = 0.0;
            for (auto& row : m_fir) {
                for (auto& fir : row) {
                    std::fill(time.begin(), time.end(), 0.0);
                    for (size_t t = 0; t < k_fir_length; ++t) {
                        time[t] = static_cast<double>(fir[t]);
                    }
                    fft.forward_inplace(time.data());
                    gmax = std::max({gmax, std::abs(time[0]), std::abs(time[1])});
                    for (size_t k = 1; k < G / 2; ++k) {
                        // sqrt(re^2 + im^2) rather than std::hypot: equivalent for
                        // these bounded magnitudes and avoids the <math.h> `hypot`
                        // macro that Windows headers (e.g. via the Max SDK) define.
                        const double re = time[2 * k];
                        const double im = time[2 * k + 1];
                        gmax            = std::max(gmax, std::sqrt(re * re + im * im));
                    }
                }
            }
            m_design_gain_db = static_cast<float>(20.0 * std::log10(std::max(gmax, 1e-12)));
            m_makeup         = static_cast<float>(std::min(1.0, 1.0 / std::max(gmax, 1e-12)));
            for (auto& row : m_fir) {
                for (auto& fir : row) {
                    for (auto& v : fir) {
                        v *= m_makeup;
                    }
                }
            }

            rebuild_convolvers();
        }

        void rebuild_convolvers() {
            m_conv.clear();
            if (m_block_size == 0) {
                return;
            }
            m_conv.reserve(4);
            for (int s = 0; s < 2; ++s) {
                for (int i = 0; i < 2; ++i) {
                    m_conv.emplace_back(m_block_size, m_fir[s][i].data(), m_fir[s][i].size());
                }
            }
            m_mix_left.assign(m_block_size, 0.0f);
            m_mix_right.assign(m_block_size, 0.0f);
            m_scratch.assign(m_block_size, 0.0f);
        }
    };

} // namespace ambitap::dsp
