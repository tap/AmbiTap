/// AmbiTap: target-independent ambisonics library
/// SN3D and N3D normalization factors for spherical harmonics.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_NORMALIZATION_H
#define AMBITAP_MATH_NORMALIZATION_H

#include <cmath>
#include <cstddef>

namespace ambitap {

    /// SN3D (Schmidt semi-normalized) factor for spherical harmonic (n, |m|).
    ///
    ///   N_n^m = sqrt((2 - delta_{m,0}) * (n - |m|)! / (n + |m|)!)
    ///
    /// Default normalization for AmbiX.
    inline float sn3d_factor(int n, int abs_m) {
        float ratio = 1.0f;
        for (int i = n - abs_m + 1; i <= n + abs_m; ++i) {
            ratio *= static_cast<float>(i);
        }
        ratio = 1.0f / ratio;

        float epsilon = (abs_m == 0) ? 1.0f : 2.0f;
        return std::sqrt(epsilon * ratio);
    }

    /// N3D factor for spherical harmonic (n, |m|).
    ///
    /// N3D = SN3D * sqrt(2n + 1). Used by some ambisonics tools (e.g., JSAmbisonics).
    inline float n3d_factor(int n, int abs_m) {
        return sn3d_factor(n, abs_m) * std::sqrt(static_cast<float>(2 * n + 1));
    }

    /// Conversion factor to scale SN3D-normalized coefficients to N3D.
    inline float sn3d_to_n3d(int order) {
        return std::sqrt(static_cast<float>(2 * order + 1));
    }

    /// Conversion factor to scale N3D-normalized coefficients to SN3D.
    inline float n3d_to_sn3d(int order) {
        return 1.0f / std::sqrt(static_cast<float>(2 * order + 1));
    }

} // namespace ambitap

#endif // AMBITAP_MATH_NORMALIZATION_H
