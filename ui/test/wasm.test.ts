/// End-to-end test of the WASM build through the same wrapper the
/// AudioWorklet uses: encode a source hard left at order 3, stream through
/// the grid + energy-vector handles, and check the analysis points at the
/// source — the node twin of the native C smoke test. Skips when the module
/// has not been built (scripts/build-wasm.sh needs emsdk).

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { AmbitapModule, Encoder, EnergyVector, Grid } from '../hosts/web/ambitap-module.js';

const wasmPath = join(dirname(fileURLToPath(import.meta.url)), '../../dist/wasm/ambitap.wasm');

let bytes: Buffer | null = null;
try {
    bytes = readFileSync(wasmPath);
} catch {
    // not built in this checkout
}

test('wasm module analyzes an encoded source correctly', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    assert.equal(mod.channelCount(3), 16);

    const encoder = new Encoder(mod, 3);
    const grid = new Grid(mod, 3, 32, 48000, 50);
    const vector = new EnergyVector(mod, 48000, 0.01);
    const N = 256;
    const mono = mod.allocFloats(N);
    const hoa = mod.allocFloats(16 * N);

    encoder.setDirection(Math.PI / 2, 0); // hard left

    let seed = 1;
    for (let block = 0; block < 64; ++block) {
        const m = mono.view;
        for (let i = 0; i < N; ++i) {
            seed = (seed * 1664525 + 1013904223) >>> 0;
            m[i] = (seed >>> 8) / 8388608 - 1;
        }
        encoder.process(mono, N, hoa);
        grid.process(hoa, N);
        vector.process(hoa, 16, N);
    }

    const { data, peakDb } = grid.snapshot(40);
    assert.equal(data.length, 16 * 32);
    let best = 0;
    for (let d = 1; d < data.length; ++d) {
        if (data[d]! > data[best]!) {
            best = d;
        }
    }
    // az +pi/2 = col 24, el 0 = row 8 (matches the native smoke test).
    assert.equal(Math.floor(best / 32), 8, 'peak row');
    assert.equal(best % 32, 24, 'peak col');
    assert.ok(Number.isFinite(peakDb));

    const [x, y, z] = vector.value();
    assert.ok(y > 0, 'energy vector +y');
    assert.ok(y > 5 * Math.abs(x) && y > 5 * Math.abs(z), 'energy vector dominated by y');

    encoder.dispose();
    grid.dispose();
    vector.dispose();
    mono.dispose();
    hoa.dispose();
});
