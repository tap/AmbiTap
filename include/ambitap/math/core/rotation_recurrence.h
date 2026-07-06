/// AmbiTap: target-independent ambisonics library
/// Real-SH rotation matrices via the Ivanic-Ruedenberg recurrence —
/// exact (to roundoff), dependency-free, allocation-free.
/// Timothy Place
/// Copyright 2026 Timothy Place.

#ifndef AMBITAP_MATH_ROTATION_RECURRENCE_H
#define AMBITAP_MATH_ROTATION_RECURRENCE_H

#include <cmath>
#include <cstddef>

#include "indexing.h"

namespace ambitap {

    namespace detail {

        /// Kronecker delta as a float factor.
        inline float kd(int a, int b) {
            return a == b ? 1.f : 0.f;
        }

    } // namespace detail

    /// Fill out[C*C] (row-major, C = (order+1)^2, ACN indexing) with the
    /// real-SH rotation matrix for the 3x3 Cartesian rotation R (row-major,
    /// acting on column vectors: d' = R d), such that Y(R d) = R_sh Y(d).
    ///
    /// Uses the Ivanic-Ruedenberg recurrence (J. Phys. Chem. 100, 6342
    /// (1996); erratum 102, 9099 (1998)): each order-l block is built from
    /// the order-(l-1) block and the order-1 block, exactly (to float
    /// roundoff) — no sampling, no least squares, no linear-algebra library.
    /// The matrix is block-diagonal per order; off-block entries are zeroed.
    ///
    /// Freestanding and allocation-free: the recurrence reads previous-order
    /// blocks directly from `out`. Part of the embedded RT profile — this is
    /// how head-tracking rotation matrices are built on-device (feed the
    /// result to dsp::matrix_applier).
    inline void compute_sh_rotation_from_matrix(int order, const float* R, float* out) {
        const size_t C = channel_count(order);
        for (size_t i = 0; i < C * C; ++i)
            out[i] = 0.f;

        // Order 0 is invariant.
        out[0] = 1.f;
        if (order < 1) return;

        // Order 1: the SH basis (ACN 1..3) is (y, z, x), so the block is the
        // Cartesian matrix conjugated by that axis permutation.
        // axis(m): m = -1 -> y(1), 0 -> z(2), +1 -> x(0).
        const int axis[3] = {1, 2, 0};
        auto      r1      = [&](int i, int j) { return R[axis[i + 1] * 3 + axis[j + 1]]; };
        for (int m = -1; m <= 1; ++m) {
            for (int n = -1; n <= 1; ++n) {
                out[acn_index(1, m) * C + acn_index(1, n)] = r1(m, n);
            }
        }

        using detail::kd;
        for (int l = 2; l <= order; ++l) {
            // Previous-order block, read from the output matrix itself.
            auto prev = [&](int a, int b) { return out[acn_index(l - 1, a) * C + acn_index(l - 1, b)]; };

            // Ivanic-Ruedenberg helper P (their eq. 8.1, with the erratum's
            // boundary cases for |n| = l).
            auto P = [&](int i, int a, int b) {
                if (b == l) return r1(i, 1) * prev(a, l - 1) - r1(i, -1) * prev(a, -(l - 1));
                if (b == -l) return r1(i, 1) * prev(a, -(l - 1)) + r1(i, -1) * prev(a, l - 1);
                return r1(i, 0) * prev(a, b);
            };

            for (int m = -l; m <= l; ++m) {
                const int   am = m < 0 ? -m : m;
                const float d  = kd(m, 0);

                for (int n = -l; n <= l; ++n) {
                    const int   an = n < 0 ? -n : n;
                    const float dn = (an == l) ? static_cast<float>(2 * l) * static_cast<float>(2 * l - 1)
                                               : static_cast<float>(l + n) * static_cast<float>(l - n);

                    const float u = std::sqrt(static_cast<float>((l + m) * (l - m)) / dn);
                    const float v =
                        0.5f * std::sqrt((1.f + d) * static_cast<float>(l + am - 1) * static_cast<float>(l + am) / dn)
                        * (1.f - 2.f * d);
                    const float w =
                        -0.5f * std::sqrt(static_cast<float>(l - am - 1) * static_cast<float>(l - am) / dn) * (1.f - d);

                    float acc = 0.f;
                    if (u != 0.f) acc += u * P(0, m, n);
                    if (v != 0.f) {
                        float V;
                        if (m == 0) {
                            V = P(1, 1, n) + P(-1, -1, n);
                        }
                        else if (m > 0) {
                            V = P(1, m - 1, n) * std::sqrt(1.f + kd(m, 1)) - P(-1, -(m - 1), n) * (1.f - kd(m, 1));
                        }
                        else {
                            V = P(1, m + 1, n) * (1.f - kd(m, -1)) + P(-1, -(m + 1), n) * std::sqrt(1.f + kd(m, -1));
                        }
                        acc += v * V;
                    }
                    if (w != 0.f) {
                        float W;
                        if (m > 0) {
                            W = P(1, m + 1, n) + P(-1, -(m + 1), n);
                        }
                        else { // w == 0 when m == 0, so m < 0 here
                            W = P(1, m - 1, n) - P(-1, -(m - 1), n);
                        }
                        acc += w * W;
                    }

                    out[acn_index(l, m) * C + acn_index(l, n)] = acc;
                }
            }
        }
    }

    /// Euler-angle entry point using the library convention (intrinsic
    /// Z-Y'-X'': yaw about +Z first, pitch about +Y second, roll about +X
    /// last; positive pitch tilts the front axis down). Equivalent to
    /// sh_rotation's angle constructor.
    inline void compute_sh_rotation(int order, float yaw, float pitch, float roll, float* out) {
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cr = std::cos(roll), sr = std::sin(roll);

        // R = Rz(yaw) * Ry(pitch) * Rx(roll), row-major, column-vector action.
        const float R[9] = {
            cy * cp,
            cy * sp * sr - sy * cr,
            cy * sp * cr + sy * sr,
            sy * cp,
            sy * sp * sr + cy * cr,
            sy * sp * cr - cy * sr,
            -sp,
            cp * sr,
            cp * cr,
        };
        compute_sh_rotation_from_matrix(order, R, out);
    }

} // namespace ambitap

#endif // AMBITAP_MATH_ROTATION_RECURRENCE_H
