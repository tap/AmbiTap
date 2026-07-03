/// AmbiTap: target-independent ambisonics library
/// Near-field compensation (NFC-HOA) — per-order bass shelving that corrects
/// the near-field effect of a spherical wave for HOA signals (Daniel's
/// formulation).
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_DSP_NFC_H
#define AMBITAP_DSP_NFC_H

#include "../math/core/indexing.h"
#include "../math/core/validate.h"
#include "util/smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ambitap::dsp {

    /// Sections in the IIR cascade of a single order-m NFC filter: ceil(m/2)
    /// (floor(m/2) biquads plus, for odd m, one first-order section).
    constexpr size_t nfc_sections_for_order(int m) {
        return static_cast<size_t>(m + 1) / 2;
    }

    /// Total cascade sections across the order-1..N filters of an order-N bus.
    constexpr size_t nfc_total_sections(int order) {
        size_t s = 0;
        for (int m = 1; m <= order; ++m) s += nfc_sections_for_order(m);
        return s;
    }

    /// Near-field compensation filter for a higher-order ambisonics bus
    /// (NFC-HOA, Jérôme Daniel's formulation).
    ///
    /// A point source at finite distance r is not a plane wave: its order-m
    /// radial term carries the spherical-Hankel near-field factor, which in the
    /// Laplace domain is the reverse Bessel polynomial ratio
    ///
    ///     F_m(s, r) = theta_m(s r / c) / (s r / c)^m,
    ///     theta_m(x) = sum_{n=0..m} (m+n)! / ((m-n)! n! 2^n) x^(m-n).
    ///
    /// Encoding a source at distance r_src for reproduction by loudspeakers
    /// (or a decoder reference) at distance r_ref requires, per order m,
    ///
    ///     H_m(s) = F_m(s, r_src) / F_m(s, r_ref)
    ///            = (r_ref / r_src)^m
    ///              · theta_m(s·r_src/c) / theta_m(s·r_ref/c),
    ///
    /// a stable m-pole / m-zero low shelf: unity at high frequency and exactly
    /// (r_ref / r_src)^m at DC — a bass boost for sources inside the reference
    /// radius, a bass cut outside it, identity when r_src == r_ref. Applied per
    /// ACN channel by its order (order 0 passes through unfiltered).
    ///
    /// Realization: the roots of theta_m (the classical Bessel-filter pole set,
    /// all in the left half-plane) are found once per order at construction;
    /// each conjugate pair — plus the one real root for odd m — becomes one
    /// section whose analog zeros are the roots scaled by c/r_src and whose
    /// poles are the same roots scaled by c/r_ref. Each section is discretized
    /// with the bilinear transform s = 2·fs·(1 - z⁻¹)/(1 + z⁻¹) (no prewarping:
    /// the mapping is exact at DC, where the shelf lives, and unity at Nyquist
    /// by construction).
    ///
    /// Numerical care: the DC boost (r_ref/r_src)^m grows without bound as
    /// r_src → 0, so both distances are clamped to k_min_distance (0.1 m).
    /// Even so, small source distances at high orders are extreme by nature —
    /// e.g. r_src = 0.1 m, r_ref = 1 m at order 5 is a +100 dB bass boost.
    /// The audio path is float32 (the embedded contract): the shelf corners
    /// sit far below fs, so coefficient quantization bounds the realized
    /// low-frequency gain to within about 0.03 dB of the closed form at
    /// 48 kHz and ordinary distances.
    ///
    /// Lifecycle: construct with the ambisonics order (validates, allocates,
    /// finds the Bessel roots); the filter is immediately usable at the default
    /// 48 kHz — call prepare() with the real sample rate before audio starts
    /// (it snaps coefficients and clears state; control thread only).
    ///
    /// Threading: setters run on one control thread; they recompute the
    /// coefficient table in double precision and publish it through a
    /// smoothed_table, which the audio thread ramps in over
    /// k_smoothing_samples. Ramping is always stable: set_source_distance()
    /// moves only numerator coefficients, and the biquad stability region in
    /// the (a1, a2) plane is convex, so a linear path between two stable
    /// denominators stays stable. process() is wait-free (no allocation,
    /// locks, or syscalls). Call snap_parameters() for offline/exact use.
    class nfc {
      public:
        /// Minimum distance both r_src and r_ref are clamped to. The order-m
        /// DC gain is (r_ref/r_src)^m, unbounded as r_src → 0; the clamp keeps
        /// the filter finite and well-conditioned at every supported order.
        static constexpr float k_min_distance = 0.1f;

      private:
        /// [b0, b1, b2, a1, a2] per section.
        static constexpr size_t k_section_coeffs = 5;
        static constexpr size_t k_max_coeffs     = k_section_coeffs * nfc_total_sections(max_order);

        int    m_order;
        size_t m_channels;
        float  m_fs {48000.0f};
        float  m_source_distance {1.0f};
        float  m_reference_distance {1.0f};
        float  m_speed_of_sound {343.0f};

        /// One root of theta_m per section: conjugate pairs stored as
        /// (re, im > 0); the odd-order real root as (re, 0).
        struct section_root {
            double re;
            double im;
        };
        std::vector<section_root> m_roots; // order-major, orders 1..m_order
        size_t                    m_total_coeffs {0};

        // Control-side snapshot and the audio-side smoothed table.
        std::array<float, k_max_coeffs> m_coefficients {};
        smoothed_table<k_max_coeffs>    m_smooth;

        // Audio-thread filter state: 2 floats per (channel, section),
        // order-major then channel-major — the traversal order of process().
        std::vector<float> m_state;

      public:
        /// @param order  Ambisonics order in [0, max_order]; channel count is
        ///               (order+1)^2. Order 0 is a valid degenerate passthrough.
        /// @throws std::invalid_argument on out-of-range order.
        explicit nfc(int order)
            : m_order(validated_order(order, "dsp::nfc"))
            , m_channels(channel_count(m_order)) {
            compute_roots();
            m_total_coeffs = k_section_coeffs * nfc_total_sections(m_order);
            m_state.assign(state_size(), 0.0f);
            rebuild_coefficients();
            m_smooth.init(m_coefficients.data(), m_total_coeffs);
        }

        int    order() const { return m_order; }
        size_t channels() const { return m_channels; }

        /// Set the sample rate: recomputes coefficients (snapped, no ramp) and
        /// clears the filter state. Control thread; call before audio starts.
        void prepare(float sample_rate) {
            m_fs = sample_rate;
            rebuild_coefficients();
            m_smooth.init(m_coefficients.data(), m_total_coeffs);
            reset();
        }

        /// Source distance in meters, clamped to k_min_distance. Control thread.
        void set_source_distance(float meters) {
            m_source_distance = std::max(meters, k_min_distance);
            rebuild_coefficients();
            publish();
        }
        float source_distance() const { return m_source_distance; }

        /// Reference (loudspeaker / decoder) distance in meters, clamped to
        /// k_min_distance. Control thread.
        void set_reference_distance(float meters) {
            m_reference_distance = std::max(meters, k_min_distance);
            rebuild_coefficients();
            publish();
        }
        float reference_distance() const { return m_reference_distance; }

        /// Speed of sound in m/s (clamped to >= 1). Control thread.
        void set_speed_of_sound(float meters_per_second) {
            m_speed_of_sound = std::max(meters_per_second, 1.0f);
            rebuild_coefficients();
            publish();
        }
        float speed_of_sound() const { return m_speed_of_sound; }

        /// Closed-form DC gain of the order-m filter: (r_ref / r_src)^m.
        float dc_gain(int order_m) const {
            return std::pow(m_reference_distance / m_source_distance, static_cast<float>(order_m));
        }

        /// Skip the coefficient ramp: the audio thread jumps straight to the
        /// latest targets on its next call. Offline rendering / tests.
        void snap_parameters() { m_smooth.snap(); }

        /// Clear the filter state; keep coefficients and allocations.
        void reset() { std::fill(m_state.begin(), m_state.end(), 0.0f); }

        /// Process one frame of channels() samples. Output may alias input.
        /// Audio thread; wait-free.
        void process_frame(const float* in, float* out) noexcept {
            const float* co   = m_smooth.tick(m_total_coeffs);
            out[0]            = in[0];
            size_t coeff_base = 0, state_off = 0;
            for (int m = 1; m <= m_order; ++m) {
                const size_t sections = nfc_sections_for_order(m);
                const size_t first    = static_cast<size_t>(m) * static_cast<size_t>(m);
                const size_t last     = channel_count(m);
                for (size_t ch = first; ch < last; ++ch) {
                    float v = in[ch];
                    for (size_t j = 0; j < sections; ++j) {
                        v = section_tick(v, co + (coeff_base + j) * k_section_coeffs,
                                         m_state.data() + state_off);
                        state_off += 2;
                    }
                    out[ch] = v;
                }
                coeff_base += sections;
            }
        }

        /// Process a block of planar channel buffers. Output may alias input.
        /// Audio thread; wait-free.
        void process(const float* const* in, float* const* out, size_t frame_count) noexcept {
            if (m_smooth.settled()) {
                // Fast path: constant coefficients, channel-major blocks.
                const float* co = m_smooth.tick(m_total_coeffs);
                if (out[0] != in[0]) {
                    for (size_t i = 0; i < frame_count; ++i) out[0][i] = in[0][i];
                }
                size_t coeff_base = 0, state_off = 0;
                for (int m = 1; m <= m_order; ++m) {
                    const size_t sections = nfc_sections_for_order(m);
                    const size_t first    = static_cast<size_t>(m) * static_cast<size_t>(m);
                    const size_t last     = channel_count(m);
                    for (size_t ch = first; ch < last; ++ch) {
                        for (size_t j = 0; j < sections; ++j) {
                            section_block(j == 0 ? in[ch] : out[ch], out[ch], frame_count,
                                          co + (coeff_base + j) * k_section_coeffs,
                                          m_state.data() + state_off);
                            state_off += 2;
                        }
                    }
                    coeff_base += sections;
                }
                return;
            }
            for (size_t i = 0; i < frame_count; ++i) {
                const float* co   = m_smooth.tick(m_total_coeffs);
                out[0][i]         = in[0][i];
                size_t coeff_base = 0, state_off = 0;
                for (int m = 1; m <= m_order; ++m) {
                    const size_t sections = nfc_sections_for_order(m);
                    const size_t first    = static_cast<size_t>(m) * static_cast<size_t>(m);
                    const size_t last     = channel_count(m);
                    for (size_t ch = first; ch < last; ++ch) {
                        float v = in[ch][i];
                        for (size_t j = 0; j < sections; ++j) {
                            v = section_tick(v, co + (coeff_base + j) * k_section_coeffs,
                                             m_state.data() + state_off);
                            state_off += 2;
                        }
                        out[ch][i] = v;
                    }
                    coeff_base += sections;
                }
            }
        }

      private:
        /// One transposed-direct-form-II tick. First-order sections are stored
        /// as biquads with b2 = a2 = 0, so one kernel serves both.
        static float section_tick(float x, const float* c, float* s) noexcept {
            const float y = c[0] * x + s[0];
            s[0]          = c[1] * x - c[3] * y + s[1];
            s[1]          = c[2] * x - c[4] * y;
            return y;
        }

        static void section_block(const float* x, float* y, size_t n, const float* c,
                                  float* s) noexcept {
            const float b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
            float       s1 = s[0], s2 = s[1];
            for (size_t i = 0; i < n; ++i) {
                const float xi = x[i];
                const float yi = b0 * xi + s1;
                s1             = b1 * xi - a1 * yi + s2;
                s2             = b2 * xi - a2 * yi;
                y[i]           = yi;
            }
            s[0] = s1;
            s[1] = s2;
        }

        size_t state_size() const {
            size_t n = 0;
            for (int m = 1; m <= m_order; ++m) {
                n += static_cast<size_t>(2 * m + 1) * nfc_sections_for_order(m) * 2;
            }
            return n;
        }

        // ---- Control-side construction (double precision; never on the
        //      audio path) -------------------------------------------------

        struct cplx {
            double re, im;
        };
        static cplx cadd(cplx a, cplx b) { return {a.re + b.re, a.im + b.im}; }
        static cplx csub(cplx a, cplx b) { return {a.re - b.re, a.im - b.im}; }
        static cplx cmul(cplx a, cplx b) {
            return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
        }
        static cplx cdiv(cplx a, cplx b) {
            const double d = b.re * b.re + b.im * b.im;
            return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
        }
        static double cabs(cplx a) { return std::sqrt(a.re * a.re + a.im * a.im); }

        /// Find the m roots of the monic reverse Bessel polynomial theta_m by
        /// Durand–Kerner iteration. theta_m's coefficients are exact integers
        /// (at most (2m)!/(m! 2^m) ≈ 6.5e8 for m = max_order) and its roots
        /// are simple and well separated, so the iteration converges quickly
        /// from a generic circular start. Construction time only.
        static void bessel_roots(int m, cplx* roots) {
            constexpr double k_two_pi = 6.28318530717958647692;

            // coeff[n] multiplies x^(m-n): (m+n)! / ((m-n)! n! 2^n).
            double coeff[max_order + 1];
            coeff[0] = 1.0;
            for (int n = 0; n < m; ++n) {
                coeff[n + 1] = coeff[n] * static_cast<double>((m + n + 1) * (m - n))
                               / (2.0 * static_cast<double>(n + 1));
            }
            const auto eval = [&](cplx z) {
                cplx p {1.0, 0.0};
                for (int n = 1; n <= m; ++n) p = cadd(cmul(p, z), cplx {coeff[n], 0.0});
                return p;
            };

            // Root magnitudes grow roughly linearly with m; start on a circle
            // of radius m, rotated off the axes so no guess is real.
            for (int i = 0; i < m; ++i) {
                const double a = k_two_pi * static_cast<double>(i) / static_cast<double>(m) + 0.5;
                roots[i]       = {static_cast<double>(m) * std::cos(a),
                                  static_cast<double>(m) * std::sin(a)};
            }
            for (int iter = 0; iter < 256; ++iter) {
                double worst = 0.0;
                for (int i = 0; i < m; ++i) {
                    cplx den {1.0, 0.0};
                    for (int j = 0; j < m; ++j) {
                        if (j != i) den = cmul(den, csub(roots[i], roots[j]));
                    }
                    const cplx delta = cdiv(eval(roots[i]), den);
                    roots[i]         = csub(roots[i], delta);
                    worst            = std::max(worst, cabs(delta) / (1.0 + cabs(roots[i])));
                }
                if (worst < 1e-14) break;
            }
        }

        /// Compute and store one section root set per order 1..m_order.
        void compute_roots() {
            m_roots.clear();
            m_roots.reserve(nfc_total_sections(m_order));
            for (int m = 1; m <= m_order; ++m) {
                cplx roots[max_order];
                bessel_roots(m, roots);

                // Conjugate pairs (im > 0), most-damped first for determinism.
                cplx pairs[max_order / 2];
                int  pair_count = 0;
                int  real_idx   = 0;
                for (int i = 1; i < m; ++i) {
                    if (std::abs(roots[i].im) < std::abs(roots[real_idx].im)) real_idx = i;
                }
                for (int i = 0; i < m; ++i) {
                    if (m % 2 == 1 && i == real_idx) continue;
                    if (roots[i].im > 0.0) pairs[pair_count++] = roots[i];
                }
                // Insertion sort: tiny fixed-size array (std::sort trips GCC's
                // -Warray-bounds here — its insertion threshold exceeds 5).
                for (int i = 1; i < pair_count; ++i) {
                    const cplx key = pairs[i];
                    int        j   = i - 1;
                    for (; j >= 0 && pairs[j].re > key.re; --j) pairs[j + 1] = pairs[j];
                    pairs[j + 1] = key;
                }
                for (int i = 0; i < pair_count; ++i) {
                    m_roots.push_back({pairs[i].re, pairs[i].im});
                }
                if (m % 2 == 1) m_roots.push_back({roots[real_idx].re, 0.0});
            }
        }

        /// Recompute the full coefficient table for the current distances,
        /// speed of sound, and sample rate. Bilinear transform per section:
        /// zeros are the theta_m roots scaled by 1/tau_src, poles the same
        /// roots scaled by 1/tau_ref (tau = r/c). Double precision, cast to
        /// float on store. Control thread; no allocation.
        void rebuild_coefficients() {
            const double c   = static_cast<double>(m_speed_of_sound);
            const double t_s = static_cast<double>(m_source_distance) / c;
            const double t_r = static_cast<double>(m_reference_distance) / c;
            const double k   = 2.0 * static_cast<double>(m_fs);
            const double k2  = k * k;
            for (size_t idx = 0; idx < m_roots.size(); ++idx) {
                const section_root r  = m_roots[idx];
                float*             cf = m_coefficients.data() + idx * k_section_coeffs;
                if (r.im == 0.0) {
                    // First-order: analog (s - z)/(s - p), z = re/t_s, p = re/t_r.
                    const double z = r.re / t_s;
                    const double p = r.re / t_r;
                    const double d = k - p; // > 0: p < 0, k > 0
                    cf[0]          = static_cast<float>((k - z) / d);
                    cf[1]          = static_cast<float>(-(k + z) / d);
                    cf[2]          = 0.0f;
                    cf[3]          = static_cast<float>(-(k + p) / d);
                    cf[4]          = 0.0f;
                }
                else {
                    // Conjugate pair: s^2 - 2 Re(x)/tau · s + |x|^2/tau^2.
                    const double m2 = r.re * r.re + r.im * r.im;
                    const double zr = r.re / t_s, zm = m2 / (t_s * t_s);
                    const double pr = r.re / t_r, pm = m2 / (t_r * t_r);
                    const double d0 = k2 - 2.0 * pr * k + pm; // > 0: pr < 0
                    cf[0]           = static_cast<float>((k2 - 2.0 * zr * k + zm) / d0);
                    cf[1]           = static_cast<float>(2.0 * (zm - k2) / d0);
                    cf[2]           = static_cast<float>((k2 + 2.0 * zr * k + zm) / d0);
                    cf[3]           = static_cast<float>(2.0 * (pm - k2) / d0);
                    cf[4]           = static_cast<float>((k2 + 2.0 * pr * k + pm) / d0);
                }
            }
        }

        void publish() { m_smooth.set(m_coefficients.data(), m_total_coeffs); }
    };

} // namespace ambitap::dsp

#endif // AMBITAP_DSP_NFC_H
