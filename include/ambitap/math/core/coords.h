/// @file coords.h
/// @brief Spherical coordinate type and angle helpers for ambisonics support code.
// SPDX-License-Identifier: MIT
// Copyright 2025-2026 Timothy Place.

#pragma once

#include <cmath>

namespace ambitap {

    /// Portable pi (M_PI is a POSIX extension MSVC hides behind
    /// _USE_MATH_DEFINES; the library avoids it).
    inline constexpr float k_pi = 3.14159265358979323846f;

    /// Spherical coordinate representing a direction on the unit sphere.
    ///
    /// Ambisonics convention:
    /// - Azimuth measured in the horizontal plane from front (0 = front, pi/2 = left).
    /// - Elevation measured from the horizontal plane (0 = horizon, pi/2 = zenith).
    struct spherical_coord {
        float azimuth;   ///< Radians. 0 = front, pi/2 = left.
        float elevation; ///< Radians. 0 = horizon, pi/2 = zenith.
    };

    /// Cartesian direction (x = front, y = left, z = up — the axis convention
    /// used throughout) to spherical angles. Well-defined at the poles and for
    /// the zero vector (atan2(0, 0) == 0).
    inline spherical_coord to_spherical(float x, float y, float z) {
        return {std::atan2(y, x), std::atan2(z, std::sqrt(x * x + y * y))};
    }

} // namespace ambitap
