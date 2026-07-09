/// Quaternion rotations and the Shoemake arcball, matching the library's
/// Euler convention (include/ambitap/math/core/rotation.h): intrinsic
/// Z-Y'-X'' — yaw about +Z first, pitch about +Y second, roll about +X last,
/// right-hand rule. Positive pitch tilts the front axis DOWN.

import { DiscFrame, Vec3 } from './coords.js';

export interface Quat {
    w: number;
    x: number;
    y: number;
    z: number;
}

export interface YawPitchRoll {
    yaw: number;
    pitch: number;
    roll: number;
}

export const QUAT_IDENTITY: Quat = { w: 1, x: 0, y: 0, z: 0 };

export function quatFromAxisAngle(ax: number, ay: number, az: number, angle: number): Quat {
    const n = Math.hypot(ax, ay, az);
    if (n === 0) {
        return { ...QUAT_IDENTITY };
    }
    const s = Math.sin(angle / 2) / n;
    return { w: Math.cos(angle / 2), x: ax * s, y: ay * s, z: az * s };
}

/** Hamilton product a*b: the rotation that applies b first, then a. */
export function quatMultiply(a: Quat, b: Quat): Quat {
    return {
        w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

export function quatConjugate(q: Quat): Quat {
    return { w: q.w, x: -q.x, y: -q.y, z: -q.z };
}

export function quatNormalize(q: Quat): Quat {
    const n = Math.hypot(q.w, q.x, q.y, q.z);
    if (n === 0) {
        return { ...QUAT_IDENTITY };
    }
    return { w: q.w / n, x: q.x / n, y: q.y / n, z: q.z / n };
}

/** Rotate a vector: q v q^-1 (q must be unit). */
export function rotateVector(q: Quat, v: Vec3): Vec3 {
    // t = 2 (qv x v); v' = v + w t + (qv x t)
    const tx = 2 * (q.y * v.z - q.z * v.y);
    const ty = 2 * (q.z * v.x - q.x * v.z);
    const tz = 2 * (q.x * v.y - q.y * v.x);
    return {
        x: v.x + q.w * tx + (q.y * tz - q.z * ty),
        y: v.y + q.w * ty + (q.z * tx - q.x * tz),
        z: v.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

/** R = Rz(yaw) * Ry(pitch) * Rx(roll), the sh_rotation angle constructor. */
export function quatFromYawPitchRoll(yaw: number, pitch: number, roll: number): Quat {
    const qz = quatFromAxisAngle(0, 0, 1, yaw);
    const qy = quatFromAxisAngle(0, 1, 0, pitch);
    const qx = quatFromAxisAngle(1, 0, 0, roll);
    return quatMultiply(qz, quatMultiply(qy, qx));
}

/** Row-major 3x3 rotation matrix — the shape sh_rotation's matrix
 *  constructor consumes. */
export function matrixFromQuat(q: Quat): number[] {
    const { w, x, y, z } = q;
    return [
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
        2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
        2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y),
    ];
}

/** Extract intrinsic Z-Y'-X'' angles. At the gimbal-lock poles
 *  (|pitch| = pi/2) roll is folded into yaw and reported as 0. */
export function yawPitchRollFromQuat(q: Quat): YawPitchRoll {
    const m = matrixFromQuat(quatNormalize(q));
    const m00 = m[0]!, m01 = m[1]!, m10 = m[3]!, m11 = m[4]!;
    const m20 = m[6]!, m21 = m[7]!, m22 = m[8]!;
    const s = Math.min(1, Math.max(-1, m20)); // m20 = -sin(pitch)
    const pitch = s === 0 ? 0 : -Math.asin(s); // avoid -0 at identity
    if (Math.abs(s) > 0.999999) {
        // Gimbal lock: only yaw ± roll is observable.
        return { yaw: Math.atan2(-m01, m11), pitch, roll: 0 };
    }
    return { yaw: Math.atan2(m10, m00), pitch, roll: Math.atan2(m21, m22) };
}

/** Shoemake arcball: map a screen point onto the unit sphere in VIEW space
 *  (x right, y up, z toward the viewer). Points outside the disc land on the
 *  rim. The rotation widget composes the result with its view orientation. */
export function arcballPoint(x: number, y: number, f: DiscFrame): Vec3 {
    let px = (x - f.cx) / f.radius;
    let py = (f.cy - y) / f.radius; // screen y-down -> view y-up
    const r2 = px * px + py * py;
    if (r2 > 1) {
        const r = Math.sqrt(r2);
        px /= r;
        py /= r;
        return { x: px, y: py, z: 0 };
    }
    return { x: px, y: py, z: Math.sqrt(1 - r2) };
}

/** The rotation taking arcball point a to arcball point b (both unit). */
export function arcballDelta(a: Vec3, b: Vec3): Quat {
    const cx = a.y * b.z - a.z * b.y;
    const cy = a.z * b.x - a.x * b.z;
    const cz = a.x * b.y - a.y * b.x;
    const dot = Math.min(1, Math.max(-1, a.x * b.x + a.y * b.y + a.z * b.z));
    const angle = Math.acos(dot);
    return quatFromAxisAngle(cx, cy, cz, angle);
}
