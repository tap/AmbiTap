import assert from 'node:assert/strict';
import { test } from 'node:test';
import { degrees } from '../core/coords.js';
import { Doa } from '../widgets/doa.js';

test('update converts the intensity vector to direction', () => {
    const doa = new Doa();
    doa.update({ x: 0, y: 1, z: 0 }); // pure left
    assert.ok(Math.abs(degrees(doa.direction().azimuth) - 90) < 1e-9);
    assert.ok(Math.abs(doa.direction().elevation) < 1e-9);
});

test('strength normalizes by the recent peak and recovers', () => {
    const doa = new Doa();
    doa.update({ x: 1, y: 0, z: 0 });
    assert.ok(doa.strength() > 0.99); // own peak
    doa.update({ x: 0.1, y: 0, z: 0 });
    assert.ok(doa.strength() < 0.2); // weak vs recent peak
});

test('a zero vector keeps the last direction (no atan2 jump to front)', () => {
    const doa = new Doa();
    doa.update({ x: 0, y: -1, z: 0 });
    const before = doa.direction().azimuth;
    doa.update({ x: 0, y: 0, z: 0 });
    assert.equal(doa.direction().azimuth, before);
});
