import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Meters } from '../widgets/meters.js';

test('levelToY maps the scale ends and clamps', () => {
    assert.equal(Meters.levelToY(0, 10, 100), 10);
    assert.equal(Meters.levelToY(-60, 10, 100), 110);
    assert.equal(Meters.levelToY(-30, 10, 100), 60);
    assert.equal(Meters.levelToY(20, 10, 100), 10); // clamp high
    assert.equal(Meters.levelToY(-200, 10, 100), 110); // clamp low
});

test('peaks hold above a falling level and decay', () => {
    const m = new Meters();
    m.update([-6]);
    m.update([-40]);
    // Peak decays by 0.35 dB per update: still near -6, far above -40.
    const peaks = (m as unknown as { peaks: number[] }).peaks;
    assert.ok(peaks[0]! > -7 && peaks[0]! < -6);
    for (let i = 0; i < 200; ++i) {
        m.update([-40]);
    }
    assert.ok(peaks[0]! <= -39.9);
});

test('channel count follows the input width', () => {
    const m = new Meters();
    m.update(new Float32Array(16).fill(-20));
    assert.equal(m.channelCount(), 16);
    m.update(new Float32Array(36).fill(-20));
    assert.equal(m.channelCount(), 36);
});
