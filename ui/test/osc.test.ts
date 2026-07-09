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
    assert.equal(bytes.length, 12);
    const tail = bytes.slice(bytes.length - 4);
    assert.deepEqual(Array.from(tail), [0x3f, 0x80, 0x00, 0x00]);
});

test('canonical padded sizes at every alignment (regression: 4-aligned strings were over-padded)', () => {
    // Independent reference: string occupies len rounded UP past a NUL to a
    // 4-byte boundary — an ALIGNED len+NUL gets no extra word.
    const stringBytes = (len: number): number => len + (4 - (len % 4));
    for (const address of [
        '/abc', // len 4: with NUL -> 8
        '/ambitap/orientation', // len 20 -> 24 (the message that worked)
        '/ambitap/source/1/direction', // len 27 -> 28 (the message oscparse dropped)
        '/ambitap/source/10/direction', // len 28 -> 32
    ]) {
        for (const n of [0, 1, 2, 3]) {
            const bytes = encodeOsc(address, new Array<number>(n).fill(0.5));
            const expected = stringBytes(address.length) + stringBytes(1 + n) + 4 * n;
            assert.equal(bytes.length, expected, `${address} with ${n} args`);
            // The typetag must start right after the address padding.
            assert.equal(bytes[stringBytes(address.length)], ','.charCodeAt(0), `${address} typetag offset`);
            const msg = decodeOsc(bytes);
            assert.equal(msg.address, address);
            assert.equal(msg.args.length, n);
        }
    }
});
