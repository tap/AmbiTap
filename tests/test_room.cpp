/// @file test_room.cpp
/// @brief Tests for dsp::room — shoebox image-source early reflections + SH-domain
///        FDN tail. Mirrors of the exact R1-R3 gates and the statistical R4/R10
///        gates from docs/PERCEPTUAL-VERIFICATION.md (the full R1-R10 suite runs in
///        notebooks/room_verification.ipynb against tools/room_render output).
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "ambitap/dsp/room.h"

using namespace tap::ambi;

namespace {

    constexpr double k_pi64  = 3.14159265358979323846;
    constexpr double k_fs    = 48000.0;
    constexpr size_t k_block = 256;

    // The seed-11 verification configuration = dsp::room's defaults.
    constexpr std::array<double, 3> k_dims{7.10, 5.30, 3.10};
    constexpr std::array<double, 3> k_source{3.674, 1.137, 1.977};
    constexpr std::array<double, 3> k_listener{1.746, 1.711, 0.668};
    constexpr std::array<double, 6> k_beta{0.90, 0.92, 0.91, 0.93, 0.89, 0.94};
    constexpr std::array<double, 5> k_rt60{0.90, 0.84, 0.76, 0.66, 0.54};
    constexpr std::array<double, 5> k_centers{250.0, 500.0, 1000.0, 2000.0, 4000.0};

    /// Render the latency-trimmed SH impulse response of a prepared-from-
    /// scratch room (prepare -> settle -> snap -> impulse through process).
    std::vector<std::vector<float>> render_ir(dsp::room& r, double seconds) {
        r.prepare(k_block, static_cast<float>(k_fs));
        r.wait_for_settling();
        r.snap_parameters();

        const size_t channels = r.channels();
        const size_t latency  = r.latency_samples();
        const size_t samples  = static_cast<size_t>(seconds * k_fs + 0.5);
        const size_t blocks   = (latency + samples + k_block - 1) / k_block;

        std::vector<float>              in(k_block, 0.0f);
        std::vector<std::vector<float>> out(channels, std::vector<float>(k_block));
        std::vector<float*>             out_ptrs;
        for (auto& b : out) {
            out_ptrs.push_back(b.data());
        }
        std::vector<std::vector<float>> ir(channels, std::vector<float>(blocks * k_block, 0.0f));

        for (size_t bi = 0; bi < blocks; ++bi) {
            std::fill(in.begin(), in.end(), 0.0f);
            if (bi == 0) {
                in[0] = 1.0f;
            }
            r.process(in.data(), out_ptrs.data(), k_block);
            for (size_t ch = 0; ch < channels; ++ch) {
                std::copy(out[ch].begin(), out[ch].end(), ir[ch].begin() + static_cast<std::ptrdiff_t>(bi * k_block));
            }
        }
        for (size_t ch = 0; ch < channels; ++ch) {
            ir[ch].erase(ir[ch].begin(), ir[ch].begin() + static_cast<std::ptrdiff_t>(latency));
            ir[ch].resize(samples);
        }
        return ir;
    }

    /// Closed-form shoebox image sources (independent mirror of the
    /// Allen-Berkley math the R1-R3 gates check against), sorted by time.
    struct image {
        double                t;
        double                amp;
        std::array<double, 3> u;
        int                   refl;
    };

    std::vector<image> image_list(double t_max) {
        constexpr double   c     = 343.0;
        const double       d_max = t_max * c;
        std::vector<image> out;
        int                n_range[3];
        for (size_t a = 0; a < 3; ++a) {
            n_range[a] = static_cast<int>(std::ceil(d_max / (2.0 * k_dims[a]))) + 1;
        }
        for (int px = 0; px <= 1; ++px) {
            for (int py = 0; py <= 1; ++py) {
                for (int pz = 0; pz <= 1; ++pz) {
                    const int p[3] = {px, py, pz};
                    for (int rx = -n_range[0]; rx <= n_range[0]; ++rx) {
                        for (int ry = -n_range[1]; ry <= n_range[1]; ++ry) {
                            for (int rz = -n_range[2]; rz <= n_range[2]; ++rz) {
                                const int r[3] = {rx, ry, rz};
                                double    v[3];
                                for (size_t a = 0; a < 3; ++a) {
                                    v[a] = (1.0 - 2.0 * p[a]) * k_source[a] + 2.0 * r[a] * k_dims[a] - k_listener[a];
                                }
                                const double dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                                const double t    = dist / c;
                                if (t > t_max || dist < 1e-6) {
                                    continue;
                                }
                                image img{};
                                img.t    = t;
                                img.amp  = 1.0;
                                img.refl = 0;
                                for (size_t a = 0; a < 3; ++a) {
                                    const int e0 = std::abs(r[a] - p[a]);
                                    const int e1 = std::abs(r[a]);
                                    img.refl += e0 + e1;
                                    img.amp *= std::pow(k_beta[2 * a], e0) * std::pow(k_beta[2 * a + 1], e1);
                                }
                                img.amp /= dist;
                                for (size_t a = 0; a < 3; ++a) {
                                    img.u[a] = v[a] / dist;
                                }
                                out.push_back(img);
                            }
                        }
                    }
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const image& a, const image& b) { return a.t < b.t; });
        return out;
    }

    // ---- Schroeder T20/EDT estimator (the notebook's, in C++): 4th-order
    //      Butterworth octave bandpass applied zero-phase (forward-backward),
    //      backward-integrated energy, least-squares line through the EDC ----

    struct biquad {
        double a1, a2; // numerator is (1, 0, -1); gain is irrelevant to a slope
    };

    std::array<biquad, 2> butter_bandpass(double center, double fs) {
        const double lo = center / std::sqrt(2.0);
        const double hi = std::min(center * std::sqrt(2.0), 0.499 * fs);
        const double wl = 2.0 * fs * std::tan(k_pi64 * lo / fs);
        const double wh = 2.0 * fs * std::tan(k_pi64 * hi / fs);
        const double w0 = std::sqrt(wl * wh);
        const double bw = wh - wl;

        // One prototype pole of the order-2 Butterworth (the other is its
        // conjugate); the lowpass->bandpass transform splits it in two, and
        // the conjugates complete each digital pole pair.
        const std::complex<double> p = std::polar(1.0, 3.0 * k_pi64 / 4.0);
        const std::complex<double> q = p * (bw / 2.0);
        const std::complex<double> d = std::sqrt(q * q - w0 * w0);
        std::array<biquad, 2>      out{};
        const std::complex<double> s_poles[2] = {q + d, q - d};
        for (size_t i = 0; i < 2; ++i) {
            const std::complex<double> z = (2.0 * fs + s_poles[i]) / (2.0 * fs - s_poles[i]);
            out[i]                       = {-2.0 * z.real(), std::norm(z)};
        }
        return out;
    }

    void sos_forward(const std::array<biquad, 2>& sos, std::vector<double>& x) {
        for (const auto& s : sos) {
            double w1 = 0.0, w2 = 0.0; // direct form II, b = (1, 0, -1)
            for (double& v : x) {
                const double w0 = v - s.a1 * w1 - s.a2 * w2;
                v               = w0 - w2;
                w2              = w1;
                w1              = w0;
            }
        }
    }

    std::vector<double> band_filtfilt(const std::vector<float>& x, double center) {
        std::vector<double> y(x.begin(), x.end());
        const auto          sos = butter_bandpass(center, k_fs);
        sos_forward(sos, y);
        std::reverse(y.begin(), y.end());
        sos_forward(sos, y);
        std::reverse(y.begin(), y.end());
        return y;
    }

    double fit_decay_time(const std::vector<double>& band, double lo_db, double hi_db) {
        std::vector<double> edc(band.size());
        double              e = 0.0;
        for (size_t i = band.size(); i-- > 0;) {
            e += band[i] * band[i];
            edc[i] = e;
        }
        const double e0 = edc[0];
        for (auto& v : edc) {
            v = 10.0 * std::log10(v / e0 + 1e-30);
        }

        size_t i0 = 0, i1 = 0;
        while (i0 < edc.size() && edc[i0] > lo_db) {
            ++i0;
        }
        i1 = i0;
        while (i1 < edc.size() && edc[i1] > hi_db) {
            ++i1;
        }
        // Least-squares slope of edc against time over [i0, i1).
        double     sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        const auto n = static_cast<double>(i1 - i0);
        for (size_t i = i0; i < i1; ++i) {
            const double t = static_cast<double>(i) / k_fs;
            sx += t;
            sy += edc[i];
            sxx += t * t;
            sxy += t * edc[i];
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        return -60.0 / slope;
    }

} // namespace

TEST(DspRoom, OrderValidation) {
    EXPECT_THROW(dsp::room(-1), std::invalid_argument);
    EXPECT_THROW(dsp::room(4), std::invalid_argument);
    dsp::room r(1);
    EXPECT_EQ(r.order(), 1);
    EXPECT_EQ(r.channels(), 4u);
}

TEST(DspRoom, SilentUntilPreparedAndOnBadBlockSize) {
    dsp::room r(1);

    std::vector<float>              in(k_block, 1.0f);
    std::vector<std::vector<float>> out(4, std::vector<float>(k_block, 0.5f));
    std::vector<float*>             ptrs;
    for (auto& b : out) {
        ptrs.push_back(b.data());
    }

    r.process(in.data(), ptrs.data(), k_block); // unprepared: must zero
    for (const auto& b : out) {
        for (float v : b) {
            ASSERT_EQ(v, 0.0f);
        }
    }

    r.prepare(192, static_cast<float>(k_fs)); // not a power of two
    EXPECT_FALSE(r.is_prepared());
    for (auto& b : out) {
        std::fill(b.begin(), b.end(), 0.5f);
    }
    r.process(in.data(), ptrs.data(), k_block);
    for (const auto& b : out) {
        for (float v : b) {
            ASSERT_EQ(v, 0.0f);
        }
    }
}

TEST(DspRoom, LatencyMatchesInjectionAlignment) {
    // max(3989 - round(0.030 * fs), 0): the causality cost of aligning all 16
    // line injections (class comment); zero once n0 passes the longest delay.
    dsp::room r(3);
    r.prepare(k_block, 48000.0f);
    EXPECT_EQ(r.latency_samples(), 3989u - 1440u);
    r.prepare(k_block, 192000.0f);
    EXPECT_EQ(r.latency_samples(), 0u);
}

TEST(DspRoom, EarlyReflectionsMatchImageSourceMath) {
    // R1-R3 mirrors: arrival times within +/-1 sample of the closed-form
    // shoebox times, per-arrival W level within 0.5 dB of prod(beta)/r, and
    // DOA within 5 deg of the image direction — measured on the rendered IR
    // against an independent enumeration of the image-source math. The
    // default geometry keeps the first 20 arrivals >= 8 samples apart, so
    // each is isolable.
    dsp::room r(3);
    r.set_tail_enabled(false);
    const auto ir     = render_ir(r, 0.05);
    const auto images = image_list(0.030);
    ASSERT_GE(images.size(), 20u);

    for (size_t k = 0; k < 20; ++k) {
        const image& img    = images[k];
        const double sample = img.t * k_fs;
        const auto   n      = static_cast<size_t>(std::llround(sample));

        // R1: the peak near the analytic time sits at the nearest sample.
        const auto lo    = static_cast<size_t>(std::floor(sample)) - 3;
        size_t     n_det = lo;
        for (size_t i = lo; i < lo + 7; ++i) {
            if (std::abs(ir[0][i]) > std::abs(ir[0][n_det])) {
                n_det = i;
            }
        }
        EXPECT_LE(std::abs(static_cast<double>(n_det) - sample), 1.0) << "arrival " << k;

        // R3: W carries the arrival amplitude directly in SN3D (Y00 = 1).
        const double e = static_cast<double>(ir[0][n - 1]) * ir[0][n - 1] + static_cast<double>(ir[0][n]) * ir[0][n]
                         + static_cast<double>(ir[0][n + 1]) * ir[0][n + 1];
        const double level_err_db = 10.0 * std::log10(e / (img.amp * img.amp));
        EXPECT_LE(std::abs(level_err_db), 0.5) << "arrival " << k;

        // R2: first-order channels encode the direction (SN3D: ACN 1/2/3 =
        // u_y, u_z, u_x times the amplitude).
        const double vx = ir[3][n], vy = ir[1][n], vz = ir[2][n];
        const double len       = std::sqrt(vx * vx + vy * vy + vz * vz);
        const double dot       = (vx * img.u[0] + vy * img.u[1] + vz * img.u[2]) / std::max(len, 1e-30);
        const double angle_deg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / k_pi64;
        EXPECT_LE(angle_deg, 5.0) << "arrival " << k;
    }
}

TEST(DspRoom, TailRealizesParameterizedRt60) {
    // R4/R5 mirrors: Schroeder T20 per octave band within +/-10% of the
    // parameterized RT60, EDT within +/-25%, on the full rendered IR (same
    // estimator as the verification notebook: zero-phase Butterworth octave
    // filter, backward integration, least-squares fit). The EDT numbers
    // carry the documented v1 bias — the frequency-flat image-source field
    // drags each band's early decay toward the Eyring rate.
    dsp::room  r(3);
    const auto ir = render_ir(r, 2.0);

    for (size_t b = 0; b < k_centers.size(); ++b) {
        const auto   band = band_filtfilt(ir[0], k_centers[b]);
        const double t20  = fit_decay_time(band, -5.0, -25.0);
        const double edt  = fit_decay_time(band, 0.0, -10.0);
        EXPECT_LE(std::abs(t20 / k_rt60[b] - 1.0), 0.10)
            << k_centers[b] << " Hz: T20 " << t20 << " vs RT60 " << k_rt60[b];
        EXPECT_LE(std::abs(edt / k_rt60[b] - 1.0), 0.25)
            << k_centers[b] << " Hz: EDT " << edt << " vs RT60 " << k_rt60[b];
    }
}

TEST(DspRoom, IirAbsorptionApproximatesRt60AndStaysCalibrated) {
    // set_absorption_kind(iir) swaps the 255-tap linear-phase absorption FIRs
    // for one first-order low-pass per line, matching the target RT60 exactly
    // only at DC and Nyquist (the cheap real-time alternative). It is an
    // approximation, so this asserts loose contracts rather than the FIR gate's
    // +/-10%: the tail must stay finite, decay on the right order across bands,
    // and — because the calibration simulates the active filter — keep the tail
    // level within a few dB of the FIR reference.
    dsp::room  rf(3);
    const auto ir_fir = render_ir(rf, 2.0);

    dsp::room ri(3);
    ri.set_absorption_kind(dsp::room::absorption_kind::iir);
    const auto ir_iir = render_ir(ri, 2.0);

    for (float v : ir_iir[0]) {
        ASSERT_TRUE(std::isfinite(v));
    }

    for (size_t b = 0; b < k_centers.size(); ++b) {
        const double t20 = fit_decay_time(band_filtfilt(ir_iir[0], k_centers[b]), -5.0, -25.0);
        EXPECT_GT(t20, 0.4 * k_rt60[b]) << k_centers[b] << " Hz: IIR T20 " << t20;
        EXPECT_LT(t20, 2.0 * k_rt60[b]) << k_centers[b] << " Hz: IIR T20 " << t20;
    }

    double ef = 0.0, ei = 0.0;
    for (float v : ir_fir[0]) {
        ef += static_cast<double>(v) * v;
    }
    for (float v : ir_iir[0]) {
        ei += static_cast<double>(v) * v;
    }
    const double ratio_db = 10.0 * std::log10(ei / std::max(ef, 1e-30));
    EXPECT_LT(std::abs(ratio_db), 3.0) << "IIR tail energy " << ratio_db << " dB vs FIR";
}

TEST(DspRoom, TailEnergyMatchesCalibrationTarget) {
    // The tail's omni energy must land on the closed-form continuation of
    // the image-source model (prototype tail_energy_target): the enumerated
    // image sum beyond the 30 ms cutoff plus the exponential extrapolation
    // of the enumeration horizon. This pins the R6 clarity contract.
    dsp::room r(3);
    r.set_direct_enabled(false);
    r.set_early_enabled(false);
    const auto ir = render_ir(r, 2.0);

    double energy = 0.0;
    for (float v : ir[0]) {
        energy += static_cast<double>(v) * v;
    }

    const double t_enum = 0.25, t_fit = 0.20, t_cut = 0.030;
    double       e_mid = 0.0, e_fit = 0.0;
    for (const auto& img : image_list(t_enum)) {
        if (img.t >= t_cut && img.t < t_enum) {
            e_mid += img.amp * img.amp;
        }
        if (img.t >= t_fit && img.t < t_enum) {
            e_fit += img.amp * img.amp;
        }
    }
    const double rate   = 13.8 / 0.76; // parameterized 1 kHz RT60
    const double target = e_mid + e_fit / (std::exp(rate * (t_enum - t_fit)) - 1.0);

    EXPECT_NEAR(energy / target, 1.0, 0.02);
}

TEST(DspRoom, DeterministicRender) {
    // R10: identical parameters render byte-identical output — the baked
    // seed-11 tables plus a fixed arithmetic order leave nothing to chance.
    dsp::room  r1(3);
    dsp::room  r2(3);
    const auto a = render_ir(r1, 0.6);
    const auto b = render_ir(r2, 0.6);
    ASSERT_EQ(a.size(), b.size());
    for (size_t ch = 0; ch < a.size(); ++ch) {
        ASSERT_EQ(0, std::memcmp(a[ch].data(), b[ch].data(), a[ch].size() * sizeof(float))) << "channel " << ch;
    }
}

TEST(DspRoom, ComponentTogglesCompose) {
    // direct/early/tail are independent contributions: the three solo
    // renders sum to the full render (to float accumulation-order noise).
    auto solo = [](bool direct, bool early, bool tail) {
        dsp::room r(1);
        r.set_direct_enabled(direct);
        r.set_early_enabled(early);
        r.set_tail_enabled(tail);
        return render_ir(r, 0.4);
    };
    const auto full   = solo(true, true, true);
    const auto direct = solo(true, false, false);
    const auto early  = solo(false, true, false);
    const auto tail   = solo(false, false, true);

    double peak = 0.0;
    for (const auto& ch : full) {
        for (float v : ch) {
            peak = std::max(peak, static_cast<double>(std::abs(v)));
        }
    }
    for (size_t ch = 0; ch < full.size(); ++ch) {
        for (size_t i = 0; i < full[ch].size(); ++i) {
            const double sum = static_cast<double>(direct[ch][i]) + early[ch][i] + tail[ch][i];
            ASSERT_NEAR(full[ch][i], sum, 1e-5 * std::max(peak, 1.0)) << "ch " << ch << " i " << i;
        }
    }
}

TEST(DspRoom, ConsistentAcrossBlockSizes) {
    // The internal chunking (min(block, 256)) and the injection-convolution
    // partitioning depend on the prepared block size; the rendered response
    // must not. Different partition counts change float rounding, so the
    // comparison is near-exact rather than bitwise.
    auto render_at_block = [](size_t block) {
        dsp::room r(1);
        r.prepare(block, static_cast<float>(k_fs));
        r.wait_for_settling();
        r.snap_parameters();
        const size_t                    total = 24576; // divisible by every block size used
        std::vector<float>              in(block, 0.0f);
        std::vector<std::vector<float>> out(4, std::vector<float>(block));
        std::vector<float*>             ptrs;
        for (auto& b : out) {
            ptrs.push_back(b.data());
        }
        std::vector<float> w(total);
        for (size_t off = 0; off < total; off += block) {
            std::fill(in.begin(), in.end(), 0.0f);
            if (off == 0) {
                in[0] = 1.0f;
            }
            r.process(in.data(), ptrs.data(), block);
            std::copy(out[0].begin(), out[0].end(), w.begin() + static_cast<std::ptrdiff_t>(off));
        }
        return w;
    };
    const auto ref = render_at_block(256);
    for (size_t block : {64u, 1024u}) {
        const auto w = render_at_block(block);
        for (size_t i = 0; i < ref.size(); ++i) {
            ASSERT_NEAR(w[i], ref[i], 1e-5f) << "block " << block << " sample " << i;
        }
    }
}

TEST(DspRoom, ParameterChangesKeepAudioFinite) {
    // Geometry / RT60 changes publish a rebuilt model that the audio thread
    // crossfades in against persistent FDN state; the output must remain
    // finite and bounded through the swap.
    dsp::room r(3);
    r.prepare(k_block, static_cast<float>(k_fs));
    r.wait_for_settling();
    r.snap_parameters();

    std::vector<float>              in(k_block);
    std::vector<std::vector<float>> out(16, std::vector<float>(k_block));
    std::vector<float*>             ptrs;
    for (auto& b : out) {
        ptrs.push_back(b.data());
    }

    unsigned seed = 1;
    auto     run  = [&](int blocks) {
        for (int bi = 0; bi < blocks; ++bi) {
            for (auto& v : in) {
                seed = seed * 1664525u + 1013904223u;
                v    = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
            }
            r.process(in.data(), ptrs.data(), k_block);
            for (const auto& ch : out) {
                for (float v : ch) {
                    ASSERT_TRUE(std::isfinite(v));
                    ASSERT_LT(std::abs(v), 100.0f);
                }
            }
        }
    };

    run(40); // ~0.2 s of noise into a settled room
    r.set_rt60(0.4f);
    r.set_room_dimensions(10.0f, 8.0f, 4.0f);
    r.set_source_position(5.0f, 4.0f, 2.0f);
    r.wait_for_settling();
    run(40); // through the model-adoption crossfade and beyond
}
