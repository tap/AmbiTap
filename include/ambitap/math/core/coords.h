/// AmbiTap: target-independent ambisonics library
/// Spherical coordinate type for ambisonics support code.
/// Timothy Place
/// Copyright 2025 Timothy Place.

#ifndef AMBITAP_MATH_COORDS_H
#define AMBITAP_MATH_COORDS_H

namespace ambitap {

    /// Spherical coordinate representing a direction on the unit sphere.
    ///
    /// Ambisonics convention:
    /// - Azimuth measured in the horizontal plane from front (0 = front, pi/2 = left).
    /// - Elevation measured from the horizontal plane (0 = horizon, pi/2 = zenith).
    struct spherical_coord {
        float azimuth;   ///< Radians. 0 = front, pi/2 = left.
        float elevation; ///< Radians. 0 = horizon, pi/2 = zenith.
    };

} // namespace ambitap

#endif // AMBITAP_MATH_COORDS_H
