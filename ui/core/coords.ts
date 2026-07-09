/// Coordinate conventions and screen mappings shared by every AmbiTap widget.
///
/// These mirror the library exactly (include/ambitap/math/core/coords.h):
/// - Azimuth: radians, 0 = front, +pi/2 = left.
/// - Elevation: radians, 0 = horizon, +pi/2 = zenith.
/// - Cartesian: x = front, y = left, z = up.
///
/// Screen space is y-down (canvas and mgraphics agree). The top-down view is
/// an orthographic projection from above with front at the top of the screen
/// and stage-left on the left of the screen.

export const HALF_PI = Math.PI / 2;
export const TWO_PI = Math.PI * 2;

/** A direction on the unit sphere, library convention (radians). */
export interface Direction {
    azimuth: number;
    elevation: number;
}

/** Cartesian direction: x = front, y = left, z = up. */
export interface Vec3 {
    x: number;
    y: number;
    z: number;
}

export function toCartesian(d: Direction): Vec3 {
    const ce = Math.cos(d.elevation);
    return {
        x: ce * Math.cos(d.azimuth),
        y: ce * Math.sin(d.azimuth),
        z: Math.sin(d.elevation),
    };
}

/** Matches ambitap::to_spherical: atan2(y, x), atan2(z, hypot(x, y)).
 *  Well-defined at the poles and for the zero vector (atan2(0, 0) == 0). */
export function toSpherical(v: Vec3): Direction {
    return {
        azimuth: Math.atan2(v.y, v.x),
        elevation: Math.atan2(v.z, Math.hypot(v.x, v.y)),
    };
}

/** Wrap an azimuth to [-pi, pi). +pi wraps to -pi (the back-wrap column of
 *  the soundfield grid). */
export function wrapAzimuth(azimuth: number): number {
    let a = (azimuth + Math.PI) % TWO_PI;
    if (a < 0) {
        a += TWO_PI;
    }
    return a - Math.PI;
}

export function clampElevation(elevation: number): number {
    return Math.min(HALF_PI, Math.max(-HALF_PI, elevation));
}

export function degrees(radians: number): number {
    return (radians * 180) / Math.PI;
}

export function radians(degrees: number): number {
    return (degrees * Math.PI) / 180;
}

/** A circular widget region in screen pixels. */
export interface DiscFrame {
    cx: number;
    cy: number;
    radius: number;
}

/** Orthographic top-down projection of a direction onto a disc: front up,
 *  left on screen-left; the point sits at cos(elevation) of the radius. */
export function topDownPoint(d: Direction, f: DiscFrame): { x: number; y: number } {
    const h = f.radius * Math.cos(d.elevation);
    return {
        x: f.cx - h * Math.sin(d.azimuth),
        y: f.cy - h * Math.cos(d.azimuth),
    };
}

/** Azimuth of a screen point in the top-down view (inverse of topDownPoint's
 *  angular part; the radial part carries elevation, which a drag leaves to
 *  the elevation control). */
export function topDownAzimuth(x: number, y: number, f: DiscFrame): number {
    return Math.atan2(f.cx - x, f.cy - y);
}

/** Equirectangular mapping matching analysis::soundfield_grid: u (column
 *  fraction) 0 = azimuth -pi (back wrap), 0.5 = front; v (row fraction)
 *  0 = zenith, 1 = nadir. */
export function directionOfEquirect(u: number, v: number): Direction {
    return {
        azimuth: -Math.PI + u * TWO_PI,
        elevation: HALF_PI - v * Math.PI,
    };
}

export function equirectOfDirection(d: Direction): { u: number; v: number } {
    return {
        u: (wrapAzimuth(d.azimuth) + Math.PI) / TWO_PI,
        v: (HALF_PI - clampElevation(d.elevation)) / Math.PI,
    };
}
