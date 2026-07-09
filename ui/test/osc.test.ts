import assert from 'node:assert/strict';
import { test } from 'node:test';
import { decodeOsc, encodeOsc } from '../core/osc.js';

test('encode/decode round trip', () => {
    const bytes = encodeOsc('/ambitap/orientation', [0.5, -0.25, 3.14159]);
    assert.equal(bytes.length % 4, 0);
    const msg = decodeOsc(bytes);
    assert.equal(msg.address, '/ambitap/orientation');
    assert.equal(msg.args.length, 3);
    assert.ok(Math.abs((msg.args[0] as number) - 0.5) < 1e-6);
    assert.ok(Math.abs((msg.args[1] as number) + 0.25) < 1e-6);
    assert.ok(Math.abs((msg.args[2] as number) - 3.14159) < 1e-5);
});

test('padding: addresses of every length remain 4-aligned', () => {
    for (const address of ['/a', '/ab', '/abc', '/abcd', '/ambitap/source/1/direction']) {
        const bytes = encodeOsc(address, [1]);
        assert.equal(bytes.length % 4, 0, address);
        assert.equal(decodeOsc(bytes).address, address);
    }
});

test('zero-argument message decodes with empty args', () => {
    const msg = decodeOsc(encodeOsc('/ping', []));
    assert.equal(msg.address, '/ping');
    assert.deepEqual(msg.args, []);
});

test('big-endian float encoding (OSC 1.0 wire format)', () => {
    const bytes = encodeOsc('/x', [1.0]);
    // '/x\0\0' ',f\0\0' then 0x3F800000.
    const tail = bytes.slice(bytes.length - 4);
    assert.deepEqual(Array.from(tail), [0x3f, 0x80, 0x00, 0x00]);
});
