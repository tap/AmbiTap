import assert from 'node:assert/strict';
import { test } from 'node:test';
import { fft, magnitudeResponse } from '../core/fft.js';

test('delta has a flat unit magnitude response', () => {
    const mags = magnitudeResponse([1], 256);
    for (const m of mags) {
        assert.ok(Math.abs(m - 1) < 1e-12);
    }
});

test('shifted delta keeps unit magnitude (pure delay)', () => {
    const fir = new Float64Array(64);
    fir[17] = 1;
    const mags = magnitudeResponse(fir, 256);
    for (const m of mags) {
        assert.ok(Math.abs(m - 1) < 1e-9);
    }
});

test('a single-bin cosine concentrates at its bin', () => {
    const n = 128;
    const k0 = 12;
    const re = new Float64Array(n);
    const im = new Float64Array(n);
    for (let i = 0; i < n; ++i) {
        re[i] = Math.cos((2 * Math.PI * k0 * i) / n);
    }
    fft(re, im);
    for (let k = 0; k <= n / 2; ++k) {
        const mag = Math.hypot(re[k]!, im[k]!);
        if (k === k0) {
            assert.ok(Math.abs(mag - n / 2) < 1e-6);
        } else {
            assert.ok(mag < 1e-6, `leak at bin ${k}: ${mag}`);
        }
    }
});

test('linearity: scaling the FIR scales the magnitude', () => {
    const fir = [0.5, -0.2, 0.1, 0.4];
    const a = magnitudeResponse(fir, 64);
    const b = magnitudeResponse(fir.map((v) => 2 * v), 64);
    for (let k = 0; k < a.length; ++k) {
        assert.ok(Math.abs(b[k]! - 2 * a[k]!) < 1e-12);
    }
});

test('non-power-of-two length is refused', () => {
    assert.throws(() => fft(new Float64Array(96), new Float64Array(96)));
});
