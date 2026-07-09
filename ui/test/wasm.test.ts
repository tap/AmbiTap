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
import { imageSources } from '../core/imagesource.js';
import {
    AmbitapModule,
    builtinHrtfHrir,
    Encoder,
    EnergyVector,
    Grid,
    roomImageSources,
    Rotator,
    XtcDesign,
} from '../hosts/web/ambitap-module.js';
import { LAYOUT_PRESETS } from '../widgets/layout.js';
import { XtcDesigner } from '../widgets/xtcdesigner.js';

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

test('wasm rotator: yaw -90deg moves a hard-left source to front', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    const encoder = new Encoder(mod, 3);
    const rotator = new Rotator(mod, 3);
    const grid = new Grid(mod, 3, 32, 48000, 50);
    const vector = new EnergyVector(mod, 48000, 0.01);
    const N = 256;
    const mono = mod.allocFloats(N);
    const hoa = mod.allocFloats(16 * N);
    const rotated = mod.allocFloats(16 * N);

    encoder.setDirection(Math.PI / 2, 0);
    rotator.setOrientation(-Math.PI / 2, 0, 0);

    let seed = 1;
    for (let block = 0; block < 64; ++block) {
        const m = mono.view;
        for (let i = 0; i < N; ++i) {
            seed = (seed * 1664525 + 1013904223) >>> 0;
            m[i] = (seed >>> 8) / 8388608 - 1;
        }
        encoder.process(mono, N, hoa);
        rotator.process(hoa, N, rotated);
        grid.process(rotated, N);
        vector.process(rotated, 16, N);
    }

    const { data } = grid.snapshot(40);
    let best = 0;
    for (let d = 1; d < data.length; ++d) {
        if (data[d]! > data[best]!) {
            best = d;
        }
    }
    assert.equal(Math.floor(best / 32), 8, 'peak row (horizon)');
    assert.equal(best % 32, 16, 'peak col (front)');
    const [x, y] = vector.value();
    assert.ok(x > 5 * Math.abs(y), 'energy vector rotated to +x');

    encoder.dispose();
    rotator.dispose();
    grid.dispose();
    vector.dispose();
    mono.dispose();
    hoa.dispose();
    rotated.dispose();
});

test('widget preset table matches ambitap_layout_preset exactly', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    for (const [name, speakers] of Object.entries(LAYOUT_PRESETS)) {
        const reference = mod.layoutPreset(name);
        assert.equal(reference.length, speakers.length, `${name}: speaker count`);
        for (let i = 0; i < speakers.length; ++i) {
            assert.ok(
                Math.abs(reference[i]!.azimuth - speakers[i]!.azimuth) < 1e-5,
                `${name}[${i}] azimuth: lib ${reference[i]!.azimuth} vs widget ${speakers[i]!.azimuth}`,
            );
            assert.ok(
                Math.abs(reference[i]!.elevation - speakers[i]!.elevation) < 1e-5,
                `${name}[${i}] elevation`,
            );
        }
    }
});

test('wasm decoder matrix has the metering shape', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    const speakers = mod.layoutPreset('7.1.4');
    const matrix = mod.decoderMatrix('allrad', 3, speakers, true);
    assert.equal(matrix.length, 11 * 16);
    assert.ok(matrix.some((v) => v !== 0));
});

test('TS image-source port matches room::for_each_image exactly', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    const dims: [number, number, number] = [7.1, 5.3, 3.1];
    const src: [number, number, number] = [3.674, 1.137, 1.977];
    const lis: [number, number, number] = [1.746, 1.711, 0.668];
    const beta = [0.9, 0.92, 0.91, 0.93, 0.89, 0.94];

    const reference = roomImageSources(mod, dims, src, lis, beta, 0.08);
    // The ABI receives float32; hand the port identical float32-rounded
    // inputs so both sides run the same double math from the same values.
    const f32 = <T extends number[]>(v: T): T => v.map(Math.fround) as T;
    const ported = imageSources(f32(dims), f32(src), f32(lis), f32(beta) as never, 0.08);
    assert.equal(ported.length, reference.length, 'image count');

    // The ABI returns float32 while the port keeps float64, so arrivals a
    // few ns apart can order-swap: match each ported arrival to an unused
    // reference arrival within tolerance instead of pairing by index.
    const sortKey = (a: { t: number }, b: { t: number }): number => a.t - b.t;
    const a = [...reference].sort(sortKey);
    const b = [...ported].sort(sortKey);
    const used = new Array<boolean>(a.length).fill(false);
    for (let i = 0; i < b.length; ++i) {
        const p = b[i]!;
        let matched = false;
        for (let j = Math.max(0, i - 4); j < Math.min(a.length, i + 5); ++j) {
            const r = a[j]!;
            if (
                !used[j] &&
                Math.abs(r.t - p.t) < 1e-6 &&
                r.reflections === p.reflections &&
                Math.abs(r.amplitude - p.amplitude) < 1e-5 * Math.max(1, r.amplitude) &&
                Math.abs(r.direction.x - p.direction.x) < 1e-4 &&
                Math.abs(r.direction.y - p.direction.y) < 1e-4 &&
                Math.abs(r.direction.z - p.direction.z) < 1e-4
            ) {
                used[j] = true;
                matched = true;
                break;
            }
        }
        assert.ok(matched, `arrival ${i} (t=${p.t}, refl=${p.reflections}) has no reference match`);
    }
});

test('xtc design pipeline: real FIRs + KEMAR plant give real in-band rejection', { skip: !bytes }, async () => {
    const module = await WebAssembly.compile(new Uint8Array(bytes!).slice().buffer);
    const mod = new AmbitapModule(module);
    const design = new XtcDesign(mod);
    design.design(30, 1.0, 0.5, 44100);
    const info = design.info();
    assert.equal(info.firLength, 1024);
    assert.equal(info.latencySamples, 512);
    assert.ok(info.designGainDb <= 12.01, `design gain ${info.designGainDb} under the ceiling`);
    assert.ok(info.makeupLinear > 0 && info.makeupLinear <= 1);

    const widget = new XtcDesigner();
    widget.setFilters(
        [
            [design.fir(0, 0), design.fir(0, 1)],
            [design.fir(1, 0), design.fir(1, 1)],
        ],
        44100,
        { designGainDb: info.designGainDb, makeupLinear: info.makeupLinear, latencySamples: info.latencySamples },
    );
    const half = (15 * Math.PI) / 180;
    widget.setPlant(builtinHrtfHrir(mod, 5, false, half, 0), builtinHrtfHrir(mod, 5, false, -half, 0));

    const rejection = widget.inBandRejectionDb();
    assert.ok(rejection !== null);
    assert.ok(rejection! > 15, `in-band rejection ${rejection} dB (expect well above 15)`);

    // The shipped |H| never exceeds unity (X5's makeup contract).
    const curves = widget.computeCurves()!;
    for (let i = 0; i < curves.freqs.length; ++i) {
        assert.ok(curves.hIpsiDb[i]! < 0.1, `|H| over unity at ${curves.freqs[i]} Hz`);
    }
    design.dispose();
});
