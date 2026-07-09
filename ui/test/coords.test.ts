import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
    clampElevation,
    directionOfEquirect,
    equirectOfDirection,
    HALF_PI,
    toCartesian,
    topDownAzimuth,
    topDownPoint,
    toSpherical,
    wrapAzimuth,
} from '../core/coords.js';

const EPS = 1e-9;

function near(actual: number, expected: number, eps = EPS): void {
    assert.ok(Math.abs(actual - expected) < eps, `expected ${expected}, got ${actual}`);
}

// Library convention (coords.h): x = front, y = left, z = up.
test('cartesian axes match the library convention', () => {
    const front = toCartesian({ azimuth: 0, elevation: 0 });
    near(front.x, 1);
    near(front.y, 0);
    near(front.z, 0);

    const left = toCartesian({ azimuth: HALF_PI, elevation: 0 });
    near(left.x, 0);
    near(left.y, 1);
    near(left.z, 0);

    const zenith = toCartesian({ azimuth: 0.7, elevation: HALF_PI });
    near(zenith.x, 0, 1e-7);
    near(zenith.y, 0, 1e-7);
    near(zenith.z, 1);
});

test('toSpherical mirrors ambitap::to_spherical, poles and zero included', () => {
    // atan2(0, 0) == 0: zero vector and exact zenith give azimuth 0.
    const zero = toSpherical({ x: 0, y: 0, z: 0 });
    near(zero.azimuth, 0);
    near(zero.elevation, 0);
    const pole = toSpherical({ x: 0, y: 0, z: 1 });
    near(pole.azimuth, 0);
    near(pole.elevation, HALF_PI);
});

test('spherical/cartesian round trip', () => {
    for (const azimuth of [-3, -1.2, 0, 0.4, 2.9]) {
        for (const elevation of [-1.5, -0.3, 0, 0.8, 1.5]) {
            const d = toSpherical(toCartesian({ azimuth, elevation }));
            near(d.azimuth, azimuth, 1e-7);
            near(d.elevation, elevation, 1e-7);
        }
    }
});

test('wrapAzimuth wraps to [-pi, pi), +pi joins the back-wrap column', () => {
    near(wrapAzimuth(Math.PI + 0.25), -Math.PI + 0.25);
    near(wrapAzimuth(-Math.PI - 0.25), Math.PI - 0.25);
    near(wrapAzimuth(Math.PI), -Math.PI);
    near(wrapAzimuth(5 * Math.PI), -Math.PI);
    near(wrapAzimuth(0.5), 0.5);
});

test('clampElevation', () => {
    near(clampElevation(2), HALF_PI);
    near(clampElevation(-2), -HALF_PI);
    near(clampElevation(0.3), 0.3);
});

test('top-down view: front up, left on screen-left, elevation pulls inward', () => {
    const f = { cx: 100, cy: 100, radius: 80 };
    const front = topDownPoint({ azimuth: 0, elevation: 0 }, f);
    near(front.x, 100);
    near(front.y, 20); // above centre

    const left = topDownPoint({ azimuth: HALF_PI, elevation: 0 }, f);
    near(left.x, 20); // screen-left
    near(left.y, 100);

    const raised = topDownPoint({ azimuth: 0, elevation: Math.PI / 3 }, f);
    near(raised.y, 100 - 80 * Math.cos(Math.PI / 3)); // cos(el) of the radius
});

test('topDownAzimuth inverts the projection angle', () => {
    const f = { cx: 100, cy: 100, radius: 80 };
    for (const azimuth of [-3, -1.5, 0, 0.9, 2.2]) {
        const p = topDownPoint({ azimuth, elevation: 0.4 }, f);
        near(topDownAzimuth(p.x, p.y, f), azimuth, 1e-7);
    }
});

test('equirect mapping matches soundfield_grid layout', () => {
    // azimuth_of_column: col 0 = -pi (back wrap); centre = front.
    let d = directionOfEquirect(0, 0);
    near(d.azimuth, -Math.PI);
    near(d.elevation, HALF_PI); // row 0 = zenith
    d = directionOfEquirect(0.5, 0.5);
    near(d.azimuth, 0);
    near(d.elevation, 0);

    const uv = equirectOfDirection({ azimuth: 0, elevation: 0 });
    near(uv.u, 0.5);
    near(uv.v, 0.5);
    for (const azimuth of [-2.5, 0, 1.1]) {
        for (const elevation of [-1.2, 0, 0.9]) {
            const rt = directionOfEquirect(
                equirectOfDirection({ azimuth, elevation }).u,
                equirectOfDirection({ azimuth, elevation }).v,
            );
            near(rt.azimuth, azimuth, 1e-7);
            near(rt.elevation, elevation, 1e-7);
        }
    }
});
