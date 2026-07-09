import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
    arcballDelta,
    arcballPoint,
    matrixFromQuat,
    Quat,
    quatFromYawPitchRoll,
    quatMultiply,
    quatNormalize,
    rotateVector,
    yawPitchRollFromQuat,
} from '../core/arcball.js';
import { Vec3 } from '../core/coords.js';

function near(actual: number, expected: number, eps = 1e-9): void {
    assert.ok(Math.abs(actual - expected) < eps, `expected ${expected}, got ${actual}`);
}

function nearVec(v: Vec3, x: number, y: number, z: number, eps = 1e-9): void {
    near(v.x, x, eps);
    near(v.y, y, eps);
    near(v.z, z, eps);
}

/** Reference R = Rz(yaw) * Ry(pitch) * Rx(roll), row-major — the exact
 *  product rotation.h builds from Eigen AngleAxis about UnitZ/UnitY/UnitX. */
function referenceMatrix(yaw: number, pitch: number, roll: number): number[] {
    const cz = Math.cos(yaw), sz = Math.sin(yaw);
    const cy = Math.cos(pitch), sy = Math.sin(pitch);
    const cx = Math.cos(roll), sx = Math.sin(roll);
    const rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
    const ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy];
    const rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx];
    return matMul(rz, matMul(ry, rx));
}

function matMul(a: number[], b: number[]): number[] {
    const out = new Array<number>(9).fill(0);
    for (let i = 0; i < 3; ++i) {
        for (let j = 0; j < 3; ++j) {
            for (let k = 0; k < 3; ++k) {
                out[i * 3 + j]! += a[i * 3 + k]! * b[k * 3 + j]!;
            }
        }
    }
    return out;
}

const FRONT: Vec3 = { x: 1, y: 0, z: 0 };
const LEFT: Vec3 = { x: 0, y: 1, z: 0 };
const HALF_PI = Math.PI / 2;

test('yaw +90deg turns front toward left (RH about +Z)', () => {
    const q = quatFromYawPitchRoll(HALF_PI, 0, 0);
    nearVec(rotateVector(q, FRONT), 0, 1, 0);
});

test('positive pitch tilts the front axis DOWN (rotation.h)', () => {
    const q = quatFromYawPitchRoll(0, HALF_PI, 0);
    nearVec(rotateVector(q, FRONT), 0, 0, -1);
});

test('roll +90deg takes left to up (RH about +X)', () => {
    const q = quatFromYawPitchRoll(0, 0, HALF_PI);
    nearVec(rotateVector(q, LEFT), 0, 0, 1);
});

test('quaternion matrix equals Rz*Ry*Rx for a sweep of angles', () => {
    for (const yaw of [-2.7, -0.9, 0, 0.6, 3.0]) {
        for (const pitch of [-1.4, -0.5, 0, 0.7, 1.4]) {
            for (const roll of [-3.0, -1.1, 0, 0.4, 2.5]) {
                const m = matrixFromQuat(quatFromYawPitchRoll(yaw, pitch, roll));
                const ref = referenceMatrix(yaw, pitch, roll);
                for (let i = 0; i < 9; ++i) {
                    near(m[i]!, ref[i]!, 1e-9);
                }
            }
        }
    }
});

test('yaw/pitch/roll extraction round trips away from gimbal lock', () => {
    for (const yaw of [-2.7, 0, 1.3]) {
        for (const pitch of [-1.2, 0, 1.2]) {
            for (const roll of [-2.1, 0, 0.8]) {
                const e = yawPitchRollFromQuat(quatFromYawPitchRoll(yaw, pitch, roll));
                near(e.yaw, yaw, 1e-7);
                near(e.pitch, pitch, 1e-7);
                near(e.roll, roll, 1e-7);
            }
        }
    }
});

test('gimbal-lock extraction still reproduces the rotation', () => {
    const q = quatFromYawPitchRoll(0.6, HALF_PI, 0.3);
    const e = yawPitchRollFromQuat(q);
    near(e.roll, 0); // roll folded into yaw by convention
    const m = matrixFromQuat(quatFromYawPitchRoll(e.yaw, e.pitch, e.roll));
    const ref = matrixFromQuat(quatNormalize(q));
    for (let i = 0; i < 9; ++i) {
        near(m[i]!, ref[i]!, 1e-6);
    }
});

test('quatMultiply composes right-to-left (Hamilton)', () => {
    const yaw: Quat = quatFromYawPitchRoll(HALF_PI, 0, 0);
    const pitch: Quat = quatFromYawPitchRoll(0, HALF_PI, 0);
    // Intrinsic yaw-then-pitch == Rz * Ry: front -> down.
    nearVec(rotateVector(quatMultiply(yaw, pitch), FRONT), 0, 0, -1);
});

test('arcball maps the disc to the view hemisphere and clamps to the rim', () => {
    const f = { cx: 50, cy: 50, radius: 40 };
    const centre = arcballPoint(50, 50, f);
    nearVec(centre, 0, 0, 1);
    const top = arcballPoint(50, 10, f); // screen up -> view +y
    nearVec(top, 0, 1, 0, 1e-7);
    const far = arcballPoint(50 + 400, 50, f); // outside -> rim
    nearVec(far, 1, 0, 0, 1e-7);
});

test('arcballDelta rotates a onto b', () => {
    const f = { cx: 0, cy: 0, radius: 100 };
    const a = arcballPoint(20, -15, f);
    const b = arcballPoint(-40, 60, f);
    const rotated = rotateVector(arcballDelta(a, b), a);
    near(rotated.x, b.x, 1e-7);
    near(rotated.y, b.y, 1e-7);
    near(rotated.z, b.z, 1e-7);
});
