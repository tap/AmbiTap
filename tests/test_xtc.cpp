/// @file test_xtc.cpp
/// @brief Tests for dsp::xtc — the numeric verification gates X1–X6 from
///        docs/PERCEPTUAL-VERIFICATION.md, computed deterministically from the
///        designed filters against the KEMAR plant model (P = C·H; no audio).
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/xtc.h"

using namespace ambitap;

namespace {

    using cd = std::complex<double>;

    constexpr double k_fs  = 44100.0; // gates run at the dataset's native rate
    constexpr double k_c   = 343.0;   // speed of sound, m/s
    constexpr double k_dpi = 3.14159265358979323846;

    /// KEMAR HRIR at (azimuth, elevation 0), reconstructed from the built-in
    /// LS SH set — the plant definition from the verification doc.
    std::vector<float> kemar_hrir(double azimuth, int ear) {
        float sh[k_max_channel_count];
        evaluate_sh(builtin_hrtf_order, static_cast<float>(azimuth), 0.0f, sh);
        std::vector<float> ir(builtin_hrtf_length, 0.0f);
        for (size_t ch = 0; ch < builtin_hrtf_channels; ++ch) {
            const float* fir = (ear == 0) ? builtin_hrtf_left[ch] : builtin_hrtf_right[ch];
            for (size_t t = 0; t < builtin_hrtf_length; ++t) {
                ir[t] += sh[ch] * fir[t];
            }
        }
        return ir;
    }

    /// Textbook DFT at an arbitrary frequency (e^{-jwt} convention) —
    /// deliberately independent of the Ooura FFT the design itself uses.
    cd dft_at(const std::vector<float>& x, double f) {
        cd           acc{0.0, 0.0};
        const double w = -2.0 * k_dpi * f / k_fs;
        for (size_t n = 0; n < x.size(); ++n) {
            const double p = w * static_cast<double>(n);
            acc += static_cast<double>(x[n]) * cd{std::cos(p), std::sin(p)};
        }
        return acc;
    }

    /// Log-spaced frequency grid (points_per_octave per octave, inclusive).
    /// Uniform log spacing makes an arithmetic mean over the grid a
    /// log-frequency band mean — the perceptually standard weighting (the
    /// gate doc smooths and budgets everything in octaves); a linear-bin
    /// mean would let the top octave of the band outvote the other three.
    std::vector<double> log_freqs(double lo, double hi, int points_per_octave) {
        std::vector<double> f;
        const int           n = static_cast<int>(std::ceil(std::log2(hi / lo) * points_per_octave));
        for (int i = 0; i <= n; ++i) {
            f.push_back(std::min(hi, lo * std::pow(2.0, static_cast<double>(i) / points_per_octave)));
        }
        return f;
    }

    /// 2x2 plant C(f) at each grid frequency: speaker s -> ear e transfer
    /// functions for speakers at ±span/2 azimuth and `distance` meters, with
    /// the head displaced `dy` meters laterally (+ = toward the left speaker)
    /// and yawed `yaw` radians (+ = turning left). Displacement re-derives
    /// per-path azimuth, 1/r gain, and propagation delay against the design
    /// geometry; at (dy = 0, yaw = 0) it reduces exactly to the design plant.
    struct plant {
        std::array<std::array<std::vector<cd>, 2>, 2> c; // [ear][speaker]
    };
    plant make_plant(double span_deg, double distance, double dy, double yaw, const std::vector<double>& freqs) {
        plant        P;
        const double half = 0.5 * span_deg * k_dpi / 180.0;
        for (size_t s = 0; s < 2; ++s) {
            const double az_spk = (s == 0) ? half : -half;
            const double px = distance * std::cos(az_spk), py = distance * std::sin(az_spk);
            const double ry      = py - dy;
            const double dist    = std::hypot(px, ry);
            const double az_head = std::atan2(ry, px) - yaw;
            const double gain    = distance / dist;
            const double dt      = (dist - distance) / k_c;
            for (size_t e = 0; e < 2; ++e) {
                const auto ir  = kemar_hrir(az_head, static_cast<int>(e));
                auto&      out = P.c[e][s];
                out.resize(freqs.size());
                for (size_t i = 0; i < freqs.size(); ++i) {
                    const double ph = -2.0 * k_dpi * freqs[i] * dt;
                    out[i]          = dft_at(ir, freqs[i]) * gain * cd{std::cos(ph), std::sin(ph)};
                }
            }
        }
        return P;
    }

    /// The four shipped filter responses H[speaker][input] on the grid.
    using filter_spectra = std::array<std::array<std::vector<cd>, 2>, 2>;
    filter_spectra filter_response(const dsp::xtc& x, const std::vector<double>& freqs) {
        filter_spectra F;
        for (size_t s = 0; s < 2; ++s) {
            for (size_t i = 0; i < 2; ++i) {
                F[s][i].resize(freqs.size());
                for (size_t k = 0; k < freqs.size(); ++k) {
                    F[s][i][k] = dft_at(x.fir(s, i), freqs[k]);
                }
            }
        }
        return F;
    }

    /// Performance matrix P(f) = C(f)·H(f) at one grid index.
    std::array<cd, 4> perf(const plant& C, const filter_spectra& F, size_t k) {
        return {C.c[0][0][k] * F[0][0][k] + C.c[0][1][k] * F[1][0][k],  // P00: L ear <- L in
                C.c[0][0][k] * F[0][1][k] + C.c[0][1][k] * F[1][1][k],  // P01: L ear <- R in
                C.c[1][0][k] * F[0][0][k] + C.c[1][1][k] * F[1][0][k],  // P10: R ear <- L in
                C.c[1][0][k] * F[0][1][k] + C.c[1][1][k] * F[1][1][k]}; // P11: R ear <- R in
    }

    /// Worse-ear crosstalk rejection XTC(f) = 20·log10(|P_ii|/|P_ij|),
    /// mean and minimum over the grid (mean over a log grid = band mean in
    /// log frequency; see log_freqs).
    struct xtc_stats {
        double mean_db;
        double min_db;
    };
    xtc_stats measure_xtc(const plant& C, const filter_spectra& F) {
        double       sum = 0.0, mn = 1e300;
        const size_t n = C.c[0][0].size();
        for (size_t k = 0; k < n; ++k) {
            const auto   p     = perf(C, F, k);
            const double left  = 20.0 * std::log10(std::abs(p[0]) / (std::abs(p[1]) + 1e-30));
            const double right = 20.0 * std::log10(std::abs(p[3]) / (std::abs(p[2]) + 1e-30));
            const double worse = std::min(left, right);
            sum += worse;
            mn = std::min(mn, worse);
        }
        return {sum / static_cast<double>(n), mn};
    }

    /// X4 coloration: |P_ii(f)|, 1/3-octave power-smoothed, referenced to its
    /// own 300 Hz – 6 kHz mean; returns the worse-ear max |deviation| in dB
    /// over 200 Hz – 8 kHz. The grid extends ±1/6 octave past the audit band
    /// so the smoother at the edges sees real data, not a clamped window.
    double measure_coloration(const plant& C, const filter_spectra& F, const std::vector<double>& freqs,
                              int points_per_octave) {
        const int    w     = points_per_octave / 6; // ±1/6 octave
        const size_t n     = freqs.size();
        double       worst = 0.0;
        for (size_t e = 0; e < 2; ++e) {
            std::vector<double> mag2(n);
            for (size_t k = 0; k < n; ++k) {
                const auto p = perf(C, F, k);
                mag2[k]      = std::norm(e == 0 ? p[0] : p[3]);
            }
            std::vector<double> smoothed(n);
            for (size_t k = 0; k < n; ++k) {
                const size_t a   = (k < static_cast<size_t>(w)) ? 0 : k - static_cast<size_t>(w);
                const size_t b   = std::min(n - 1, k + static_cast<size_t>(w));
                double       acc = 0.0;
                for (size_t j = a; j <= b; ++j) {
                    acc += mag2[j];
                }
                smoothed[k] = 10.0 * std::log10(acc / static_cast<double>(b - a + 1) + 1e-30);
            }
            double ref = 0.0;
            size_t nr  = 0;
            for (size_t k = 0; k < n; ++k) {
                if (freqs[k] >= 300.0 && freqs[k] <= 6000.0) {
                    ref += smoothed[k];
                    ++nr;
                }
            }
            ref /= static_cast<double>(nr);
            for (size_t k = 0; k < n; ++k) {
                if (freqs[k] < 200.0 || freqs[k] > 8000.0) {
                    continue;
                }
                worst = std::max(worst, std::abs(smoothed[k] - ref));
            }
        }
        return worst;
    }

    /// Shared analysis grids. 240 points/octave puts ~17 Hz between points at
    /// the 6 kHz band edge — denser than the ~43 Hz ripple period of the
    /// 1024-tap realization, so the minimum can't hide between points.
    const std::vector<double>& band_grid() {
        static const auto g = log_freqs(dsp::xtc::k_band_low_hz, dsp::xtc::k_band_high_hz, 240);
        return g;
    }
    const std::vector<double>& audit_grid() {
        static const auto g = log_freqs(200.0 * std::pow(2.0, -1.0 / 6.0), 8000.0 * std::pow(2.0, 1.0 / 6.0), 96);
        return g;
    }

    /// A designed canceller at the dataset-native rate (no convolvers needed:
    /// the gates read the shipped FIRs).
    dsp::xtc make_design(float span_deg) {
        dsp::xtc x;
        x.set_span(span_deg);
        return x;
    }

} // namespace

// X1 — crosstalk rejection spectrum: worse-ear XTC(f) >= 20 dB mean and
// >= 15 dB minimum over 300 Hz – 6 kHz, at spans ±10°, ±20°, ±30°.
// Margins as designed: mean 60.7/58.5/62.8 dB, min 23.1/40.0/41.3 dB.
TEST(DspXtc, X1CrosstalkRejectionSpectrum) {
    for (const float span : {20.0f, 40.0f, 60.0f}) {
        const auto x = make_design(span);
        const auto F = filter_response(x, band_grid());
        const auto C = make_plant(span, 1.0, 0.0, 0.0, band_grid());
        const auto s = measure_xtc(C, F);
        EXPECT_GE(s.mean_db, 20.0) << "span " << span;
        EXPECT_GE(s.min_db, 15.0) << "span " << span;
    }
}

// X2 — robustness to head translation, filters held fixed, at the design
// point (span 20°, 1 m): >= 12 dB mean in-band XTC at ±2 cm lateral, and
// XTC no worse than bypass at ±5 cm.
//
// The ±5 cm clause is gated on the BAND MEAN. A strict per-frequency
// reading (min >= 0 dB) is provably unreachable together with X1: a 5 cm
// lateral shift skews the two path lengths by ~2·dy·sin(span/2) ≈ 17 mm
// (~50 us), so at 6 kHz the cancellation signal arrives ~1.9 rad out of
// phase — any filter that cancels deeply at the design point (X1 demands
// >= 15 dB at 6 kHz) must push the displaced 6 kHz XTC below 0 dB there
// (measured: ~-2 dB at the top of the band, mean ~+7.8 dB). Choosing
// shallower HF cancellation to protect the ±5 cm minimum would fail X1
// outright, so the mean — "approximately ordinary stereo on average", the
// stated intent — is the implementable contract.
TEST(DspXtc, X2RobustnessTranslation) {
    const float span = 20.0f;
    const auto  x    = make_design(span);
    const auto  F    = filter_response(x, band_grid());
    for (const double dy : {0.02, -0.02}) {
        const auto C = make_plant(span, 1.0, dy, 0.0, band_grid());
        const auto s = measure_xtc(C, F);
        EXPECT_GE(s.mean_db, 12.0) << "dy " << dy; // designed margin: 16.8 dB
    }
    for (const double dy : {0.05, -0.05}) {
        const auto C = make_plant(span, 1.0, dy, 0.0, band_grid());
        const auto s = measure_xtc(C, F);
        EXPECT_GE(s.mean_db, 0.0) << "dy " << dy; // designed margin: 7.8 dB
    }
}

// X3 — robustness to head rotation (yaw), filters held fixed, at the design
// point: >= 12 dB mean in-band at ±5°, >= 6 dB at ±10°. Rotation is
// first-order benign (the inter-speaker arrival-time difference at an ear
// varies with cos(yaw)), so these margins are wide: 24.3 dB and 18.1 dB.
TEST(DspXtc, X3RobustnessRotation) {
    const float span = 20.0f;
    const auto  x    = make_design(span);
    const auto  F    = filter_response(x, band_grid());
    for (const double yaw_deg : {5.0, -5.0}) {
        const auto C = make_plant(span, 1.0, 0.0, yaw_deg * k_dpi / 180.0, band_grid());
        EXPECT_GE(measure_xtc(C, F).mean_db, 12.0) << "yaw " << yaw_deg;
    }
    for (const double yaw_deg : {10.0, -10.0}) {
        const auto C = make_plant(span, 1.0, 0.0, yaw_deg * k_dpi / 180.0, band_grid());
        EXPECT_GE(measure_xtc(C, F).mean_db, 6.0) << "yaw " << yaw_deg;
    }
}

// X4 — coloration budget: 1/3-octave-smoothed |P_ii| within ±3 dB of its
// 300 Hz – 6 kHz mean over 200 Hz – 8 kHz at the design point (all three
// X1 spans; measured 0.8/0.7/0.2 dB), and within ±6 dB under the X2/X3
// offsets at the design geometry (worst measured: 3.1 dB at ±5 cm).
TEST(DspXtc, X4ColorationBudget) {
    for (const float span : {20.0f, 40.0f, 60.0f}) {
        const auto x = make_design(span);
        const auto F = filter_response(x, audit_grid());
        const auto C = make_plant(span, 1.0, 0.0, 0.0, audit_grid());
        EXPECT_LE(measure_coloration(C, F, audit_grid(), 96), 3.0) << "span " << span;
    }

    const float span = 20.0f;
    const auto  x    = make_design(span);
    const auto  F    = filter_response(x, audit_grid());
    for (const double dy : {0.02, -0.02, 0.05, -0.05}) {
        const auto C = make_plant(span, 1.0, dy, 0.0, audit_grid());
        EXPECT_LE(measure_coloration(C, F, audit_grid(), 96), 6.0) << "dy " << dy;
    }
    for (const double yaw_deg : {5.0, -5.0, 10.0, -10.0}) {
        const auto C = make_plant(span, 1.0, 0.0, yaw_deg * k_dpi / 180.0, audit_grid());
        EXPECT_LE(measure_coloration(C, F, audit_grid(), 96), 6.0) << "yaw " << yaw_deg;
    }
}

// X5 — filter gain ceiling: the designed (pre-makeup) response never exceeds
// +12 dB at any frequency in any of the four filters, and the shipped FIRs
// carry a matching static makeup attenuation so no single path exceeds
// unity gain. Checked on a dense linear grid up to Nyquist (independent of
// the design's own FFT grid).
TEST(DspXtc, X5FilterGainCeiling) {
    for (const float span : {20.0f, 40.0f, 60.0f}) {
        const auto x = make_design(span);
        EXPECT_LE(x.design_gain_db(), 12.0 + 1e-4) << "span " << span;
        EXPECT_LE(x.makeup_gain(), 1.0f) << "span " << span;

        double shipped_max = 0.0;
        for (size_t s = 0; s < 2; ++s) {
            for (size_t i = 0; i < 2; ++i) {
                for (int k = 0; k <= 2048; ++k) {
                    const double f = k_fs / 2.0 * static_cast<double>(k) / 2048.0;
                    shipped_max    = std::max(shipped_max, std::abs(dft_at(x.fir(s, i), f)));
                }
            }
        }
        // Shipped = designed × makeup, so this is the "matching" check: the
        // worst shipped gain must sit at unity (within grid quantization),
        // never above it — and the reported makeup is exactly the reciprocal
        // of the reported design gain.
        EXPECT_LE(shipped_max, 1.0 + 1e-3) << "span " << span;
        EXPECT_GT(shipped_max, 0.9) << "span " << span;
        EXPECT_NEAR(static_cast<double>(x.makeup_gain())
                        * std::pow(10.0, static_cast<double>(x.design_gain_db()) / 20.0),
                    1.0, 1e-3)
            << "span " << span;
    }
}

// X6 — determinism: designing twice at identical parameters yields
// byte-identical filters, including through parameter churn and at a
// resampled host rate.
TEST(DspXtc, X6Determinism) {
    const auto identical = [](const dsp::xtc& a, const dsp::xtc& b) {
        for (size_t s = 0; s < 2; ++s) {
            for (size_t i = 0; i < 2; ++i) {
                const auto& fa = a.fir(s, i);
                const auto& fb = b.fir(s, i);
                if (fa.size() != fb.size()) {
                    return false;
                }
                if (std::memcmp(fa.data(), fb.data(), fa.size() * sizeof(float)) != 0) {
                    return false;
                }
            }
        }
        return a.makeup_gain() == b.makeup_gain();
    };

    dsp::xtc a, b;
    EXPECT_TRUE(identical(a, b));

    a.set_span(40.0f);
    a.set_regularization(0.3f);
    a.set_sample_rate(48000.0f);
    b.set_sample_rate(48000.0f);
    b.set_regularization(0.3f);
    b.set_span(40.0f); // different setter order, same final parameters
    EXPECT_TRUE(identical(a, b));

    // Churn back and forth: the design must depend only on current values.
    a.set_span(120.0f);
    a.set_span(40.0f);
    EXPECT_TRUE(identical(a, b));
}

// The audio path realizes exactly the shipped FIRs: an impulse into one
// input reproduces that input's two filters (partitioned convolution across
// many blocks), and the object is silent before prepare() or on a block-size
// mismatch.
TEST(DspXtc, ProcessRealizesShippedFirs) {
    dsp::xtc x;

    // Unprepared: silence.
    std::vector<float> in(64, 1.0f), out_l(64, -1.0f), out_r(64, -1.0f);
    x.process(in.data(), in.data(), out_l.data(), out_r.data(), 64);
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_EQ(out_l[i], 0.0f);
        ASSERT_EQ(out_r[i], 0.0f);
    }

    x.prepare(64, static_cast<float>(k_fs));
    ASSERT_TRUE(x.is_prepared());
    EXPECT_EQ(x.latency_samples(), dsp::xtc::k_fir_length / 2);

    // Mismatched block: silence (and no state corruption).
    x.process(in.data(), in.data(), out_l.data(), out_r.data(), 32);
    for (size_t i = 0; i < 32; ++i) {
        ASSERT_EQ(out_l[i], 0.0f);
    }

    // Impulse on the left input -> fir(0,0) on the left speaker, fir(1,0) on
    // the right speaker.
    const size_t       blocks = (dsp::xtc::k_fir_length + 64) / 64;
    std::vector<float> zeros(64, 0.0f), got_l, got_r;
    for (size_t b = 0; b < blocks; ++b) {
        std::vector<float> imp(64, 0.0f);
        if (b == 0) {
            imp[0] = 1.0f;
        }
        x.process(imp.data(), zeros.data(), out_l.data(), out_r.data(), 64);
        got_l.insert(got_l.end(), out_l.begin(), out_l.end());
        got_r.insert(got_r.end(), out_r.begin(), out_r.end());
    }
    for (size_t t = 0; t < dsp::xtc::k_fir_length; ++t) {
        ASSERT_NEAR(got_l[t], x.fir(0, 0)[t], 2e-5f) << "t=" << t;
        ASSERT_NEAR(got_r[t], x.fir(1, 0)[t], 2e-5f) << "t=" << t;
    }
}

// Designs at common host rates stay finite, under the ceiling, and usable;
// the built-in 44.1 kHz KEMAR set is resampled at design time.
TEST(DspXtc, DesignAtCommonHostRates) {
    for (const float rate : {44100.0f, 48000.0f, 96000.0f}) {
        dsp::xtc x;
        x.prepare(128, rate);
        EXPECT_LE(x.design_gain_db(), 12.0f + 1e-4f) << rate;
        double energy = 0.0;
        for (size_t s = 0; s < 2; ++s) {
            for (size_t i = 0; i < 2; ++i) {
                ASSERT_EQ(x.fir(s, i).size(), dsp::xtc::k_fir_length);
                for (const float v : x.fir(s, i)) {
                    ASSERT_TRUE(std::isfinite(v));
                    energy += static_cast<double>(v) * static_cast<double>(v);
                }
            }
        }
        EXPECT_GT(energy, 0.1) << rate;

        std::vector<float> in(128, 0.5f), l(128), r(128);
        x.process(in.data(), in.data(), l.data(), r.data(), 128);
        for (const float v : l) {
            ASSERT_TRUE(std::isfinite(v));
        }
    }
}
