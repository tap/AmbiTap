/// AmbiTap: target-independent ambisonics library
/// Real spherical harmonic evaluation using ACN/SN3D (AmbiX convention).
/// Timothy Place
/// Copyright 2025-2026 Timothy Place.

#ifndef AMBITAP_MATH_SPHERICAL_HARMONICS_H
#define AMBITAP_MATH_SPHERICAL_HARMONICS_H

#include "coords.h"
#include "indexing.h"
#include "normalization.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace ambitap {

    /// Evaluate all real spherical harmonics up to the given order at a direction,
    /// using ACN ordering and SN3D normalization (AmbiX convention).
    ///
    /// @param order      Ambisonics order. Must be <= max_order.
    /// @param azimuth    Horizontal angle, radians. 0 = front, pi/2 = left.
    /// @param elevation  Angle from horizontal, radians. 0 = horizon, pi/2 = zenith.
    /// @param out        Output array of size >= (order+1)^2.
    ///
    /// Standard recurrence for associated Legendre polynomials (no Condon-Shortley phase).
    ///
    /// References:
    ///   Zotter, F. (2009). "Analysis and Synthesis of Sound-Radiation with Spherical Arrays".
    /// Implemented directly from the published formulas; no third-party SH code is used.
    inline void evaluate_sh(int order, float azimuth, float elevation, float* out) {
        // The plm table below (and the caller's out array) are sized for
        // max_order; an out-of-range order would silently corrupt the stack.
        // Public entry points validate their order arguments (validate.h), so
        // this clamp is a last-resort memory-safety guard, not an API.
        assert(order >= 0 && order <= max_order);
        order = std::clamp(order, 0, max_order);

        const float sin_el = std::sin(elevation);
        const float cos_el = std::cos(elevation);

        // Associated Legendre polynomials P_n^m(sin_el) without Condon-Shortley phase,
        // for all n <= order, 0 <= m <= n. Stored as a flat (max_order+1) x (max_order+1)
        // table; we only need m >= 0 since the m < 0 normalization is absorbed in sn3d_factor.
        const int N = order;
        float     plm[(max_order + 1) * (max_order + 1)];

        // Sectoral recurrence: P_m^m
        plm[0] = 1.0f; // P_0^0 = 1
        for (int m = 1; m <= N; ++m) {
            plm[m * (max_order + 1) + m] =
                static_cast<float>(2 * m - 1) * cos_el * plm[(m - 1) * (max_order + 1) + (m - 1)];
        }

        // First off-diagonal: P_{m+1}^m
        for (int m = 0; m < N; ++m) {
            plm[(m + 1) * (max_order + 1) + m] =
                static_cast<float>(2 * m + 1) * sin_el * plm[m * (max_order + 1) + m];
        }

        // General recurrence: P_n^m for n > m+1
        for (int m = 0; m <= N; ++m) {
            for (int n = m + 2; n <= N; ++n) {
                plm[n * (max_order + 1) + m] =
                    (static_cast<float>(2 * n - 1) * sin_el * plm[(n - 1) * (max_order + 1) + m]
                     - static_cast<float>(n + m - 1) * plm[(n - 2) * (max_order + 1) + m])
                    / static_cast<float>(n - m);
            }
        }

        // Assemble real SH with SN3D normalization
        for (int n = 0; n <= N; ++n) {
            for (int m = -n; m <= n; ++m) {
                const int   abs_m = std::abs(m);
                const float norm  = sn3d_factor(n, abs_m);
                const float p     = plm[n * (max_order + 1) + abs_m];

                float y;
                if (m > 0) {
                    y = norm * p * std::cos(static_cast<float>(m) * azimuth);
                } else if (m < 0) {
                    y = norm * p * std::sin(static_cast<float>(abs_m) * azimuth);
                } else {
                    y = norm * p;
                }

                out[acn_index(n, m)] = y;
            }
        }
    }

    /// Convenience overload taking a spherical_coord.
    inline void evaluate_sh(int order, spherical_coord coord, float* out) {
        evaluate_sh(order, coord.azimuth, coord.elevation, out);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_SPHERICAL_HARMONICS_H
