import assert from 'node:assert/strict';
import { test } from 'node:test';
import { imageSources, SPEED_OF_SOUND } from '../core/imagesource.js';

const DIMS: [number, number, number] = [7.1, 5.3, 3.1];
const SRC: [number, number, number] = [3.674, 1.137, 1.977];
const LIS: [number, number, number] = [1.746, 1.711, 0.668];
const BETA: [number, number, number, number, number, number] = [0.9, 0.92, 0.91, 0.93, 0.89, 0.94];

test('direct path: exact time, amplitude 1/d, zero reflections', () => {
    const images = imageSources(DIMS, SRC, LIS, BETA, 0.05);
    const direct = images.filter((i) => i.reflections === 0);
    assert.equal(direct.length, 1);
    const d = Math.hypot(SRC[0] - LIS[0], SRC[1] - LIS[1], SRC[2] - LIS[2]);
    assert.ok(Math.abs(direct[0]!.t - d / SPEED_OF_SOUND) < 1e-12);
    assert.ok(Math.abs(direct[0]!.amplitude - 1 / d) < 1e-12);
});

test('first-order floor reflection: image mirrored below z=0, amplitude beta_z0 / d', () => {
    const images = imageSources(DIMS, SRC, LIS, BETA, 0.05);
    // The z0 (floor) image: source mirrored to -z.
    const mirrored = [SRC[0], SRC[1], -SRC[2]];
    const d = Math.hypot(mirrored[0]! - LIS[0], mirrored[1]! - LIS[1], mirrored[2]! - LIS[2]);
    const hit = images.find(
        (i) => i.reflections === 1 && Math.abs(i.t - d / SPEED_OF_SOUND) < 1e-9 && i.direction.z < 0,
    );
    assert.ok(hit, 'floor image present');
    assert.ok(Math.abs(hit!.amplitude - BETA[4] / d) < 1e-9, `amp ${hit!.amplitude} vs ${BETA[4] / d}`);
});

test('every image obeys the time window and unit directions', () => {
    const tMax = 0.04;
    const images = imageSources(DIMS, SRC, LIS, BETA, tMax);
    assert.ok(images.length > 50);
    for (const img of images) {
        assert.ok(img.t <= tMax);
        assert.ok(img.amplitude > 0);
        const n = Math.hypot(img.direction.x, img.direction.y, img.direction.z);
        assert.ok(Math.abs(n - 1) < 1e-9);
    }
});

test('higher wall absorption (lower beta) lowers reflection amplitudes, not the direct', () => {
    const lively = imageSources(DIMS, SRC, LIS, BETA, 0.03);
    const dead = imageSources(DIMS, SRC, LIS, [0.3, 0.3, 0.3, 0.3, 0.3, 0.3], 0.03);
    const key = (i: { t: number }): number => Math.round(i.t * 1e9);
    const deadByT = new Map(dead.map((i) => [key(i), i]));
    for (const img of lively) {
        const twin = deadByT.get(key(img));
        assert.ok(twin, 'same geometry, same arrivals');
        if (img.reflections === 0) {
            assert.ok(Math.abs(twin!.amplitude - img.amplitude) < 1e-12);
        } else {
            assert.ok(twin!.amplitude < img.amplitude);
        }
    }
});
