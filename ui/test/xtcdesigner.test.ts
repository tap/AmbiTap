import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Renderer, RGBA } from '../render/renderer.js';
import { XtcDesigner } from '../widgets/xtcdesigner.js';

class StubRenderer implements Renderer {
    texts: string[] = [];
    constructor(
        public readonly width: number,
        public readonly height: number,
    ) {}
    save(): void {}
    restore(): void {}
    beginPath(): void {}
    moveTo(): void {}
    lineTo(): void {}
    arc(): void {}
    rect(): void {}
    closePath(): void {}
    setColor(_c: RGBA): void {}
    setLineWidth(): void {}
    fill(): void {}
    stroke(): void {}
    setFontSize(): void {}
    fillText(text: string): void {
        this.texts.push(text);
    }
    textWidth(text: string): number {
        return text.length * 5;
    }
}

function delta(length: number, at = 0, gain = 1): Float32Array {
    const fir = new Float32Array(length);
    fir[at] = gain;
    return fir;
}

test('identity filters + identity plant: flat ipsi, floored contra, huge rejection', () => {
    const xtc = new XtcDesigner();
    // H = I (pure pass-through), C = I (no acoustic crosstalk).
    xtc.setFilters(
        [
            [delta(64), new Float32Array(64)],
            [new Float32Array(64), delta(64)],
        ],
        44100,
    );
    xtc.setPlant({ left: delta(64), right: new Float32Array(64) },
        { left: new Float32Array(64), right: delta(64) });

    const curves = xtc.computeCurves()!;
    for (let i = 0; i < curves.freqs.length; ++i) {
        assert.ok(Math.abs(curves.hIpsiDb[i]!) < 1e-6, 'H ipsi flat 0 dB');
        assert.ok(curves.pIpsiDb![i]! > -1e-6 && curves.pIpsiDb![i]! < 1e-6, 'P ipsi flat 0 dB');
        assert.ok(curves.pContraDb![i]! < -100, 'P contra at the floor');
    }
    assert.ok(xtc.inBandRejectionDb()! > 100);
});

test('crosstalk-only plant with identity filters gives ZERO rejection', () => {
    const xtc = new XtcDesigner();
    xtc.setFilters(
        [
            [delta(64), new Float32Array(64)],
            [new Float32Array(64), delta(64)],
        ],
        44100,
    );
    // Both ears hear both speakers equally: no separation at all.
    xtc.setPlant({ left: delta(64), right: delta(64) }, { left: delta(64), right: delta(64) });
    const rejection = xtc.inBandRejectionDb()!;
    assert.ok(Math.abs(rejection) < 1e-6, `rejection ${rejection}`);
});

test('speaker drag sets span from angle and distance from radius', () => {
    const xtc = new XtcDesigner();
    xtc.draw(new StubRenderer(680, 300));
    const panes = xtc.layout(680, 300);
    // Recreate the geometry mapping (head at 78% height, scale = 0.62h / 3m).
    const cx = panes.geo.x + panes.geo.w / 2;
    const cy = panes.geo.y + panes.geo.h * 0.78;
    const scale = (panes.geo.h * 0.62) / 3;

    // Grab the left speaker (span 20 deg -> +10 deg azimuth, 1 m out).
    const az0 = (10 * Math.PI) / 180;
    assert.ok(xtc.pointerDown(cx - scale * Math.sin(az0), cy - scale * Math.cos(az0), 1000));
    // Drag to azimuth +30 deg at 1.5 m: span 60, distance 1.5.
    const az1 = (30 * Math.PI) / 180;
    xtc.pointerMove(cx - 1.5 * scale * Math.sin(az1), cy - 1.5 * scale * Math.cos(az1), 1010);
    xtc.pointerUp(1020);

    assert.ok(Math.abs(xtc.params.spanDeg - 60) < 0.5, `span ${xtc.params.spanDeg}`);
    assert.ok(Math.abs(xtc.params.distance - 1.5) < 0.02, `distance ${xtc.params.distance}`);
});

test('regularization slider maps its track to 0..1 and gates external sets', () => {
    const xtc = new XtcDesigner();
    xtc.draw(new StubRenderer(680, 300));
    const panes = xtc.layout(680, 300);
    const midX = panes.slider.x + 6 + (panes.slider.w - 12) / 2;
    assert.ok(xtc.pointerDown(midX, panes.slider.y + panes.slider.h / 2, 1000));
    assert.ok(Math.abs(xtc.params.regularization - 0.5) < 0.02);
    assert.equal(xtc.setParams({ spanDeg: 90, distance: 2, regularization: 0 }, 1001), false);
    xtc.pointerUp(1002);
    assert.ok(xtc.setParams({ spanDeg: 90, distance: 2, regularization: 0 }, 2000));
    assert.equal(xtc.params.spanDeg, 90);
});

test('span and distance clamp to the library ranges', () => {
    const xtc = new XtcDesigner();
    xtc.setParams({ spanDeg: 999, distance: 99, regularization: 7 }, 0);
    assert.equal(xtc.params.spanDeg, 120);
    assert.equal(xtc.params.distance, 3);
    assert.equal(xtc.params.regularization, 1);
});
