import assert from 'node:assert/strict';
import { test } from 'node:test';
import { HALF_PI } from '../core/coords.js';
import { Renderer, RGBA } from '../render/renderer.js';
import { heatColor, Heatmap } from '../widgets/heatmap.js';

class CountingRenderer implements Renderer {
    fills = 0;
    rects = 0;
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
    rect(): void { this.rects++; }
    closePath(): void {}
    setColor(_c: RGBA): void {}
    setLineWidth(): void {}
    fill(): void { this.fills++; }
    stroke(): void {}
    setFontSize(): void {}
    fillText(text: string): void { this.texts.push(text); }
    textWidth(text: string): number { return text.length * 5; }
}

function image(rows: number, cols: number): Float32Array {
    return new Float32Array(rows * cols);
}

test('directionToXY: grid layout landmarks land on their cells', () => {
    const h = new Heatmap();
    h.setImage(image(16, 32), 16, 32, -10);
    const frame = { x: 0, y: 0, w: 320, h: 160 };

    // Front (az 0) = centre column (col 16 of 32 -> cell centre x = (16+0.5)/32*320).
    const front = h.directionToXY({ azimuth: 0, elevation: 0 }, frame);
    assert.ok(Math.abs(front.x - ((16 + 0.5) / 32) * 320) < 1e-9);
    // Horizon (el 0) = row 8 cell centre.
    assert.ok(Math.abs(front.y - ((8 + 0.5) / 16) * 160) < 1e-9);

    // Azimuth -pi = column 0 (the back wrap).
    const back = h.directionToXY({ azimuth: -Math.PI, elevation: 0 }, frame);
    assert.ok(Math.abs(back.x - (0.5 / 32) * 320) < 1e-9);

    // Zenith = row 0.
    const zenith = h.directionToXY({ azimuth: 0, elevation: HALF_PI }, frame);
    assert.ok(Math.abs(zenith.y - (0.5 / 16) * 160) < 1e-9);

    // Left (+pi/2) sits in the right half of the image (az -pi..pi left to right).
    const left = h.directionToXY({ azimuth: HALF_PI, elevation: 0 }, frame);
    assert.ok(left.x > 320 * 0.7);
});

test('heatColor is monotonically brighter and clamps', () => {
    const luma = (v: number): number => {
        const c = heatColor(v);
        return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    };
    let prev = -1;
    for (let v = 0; v <= 1.0001; v += 0.05) {
        const l = luma(Math.min(v, 1));
        assert.ok(l >= prev - 1e-9, `luma not monotonic at ${v}`);
        prev = l;
    }
    assert.deepEqual(heatColor(-5), heatColor(0));
    assert.deepEqual(heatColor(5), heatColor(1));
});

test('writeRGBA fills every pixel opaquely at grid resolution', () => {
    const h = new Heatmap();
    const data = image(4, 8);
    data[2 * 8 + 3] = 1;
    h.setImage(data, 4, 8, -3);
    const dst = new Uint8ClampedArray(4 * 8 * 4);
    h.writeRGBA(dst);
    for (let p = 0; p < 32; ++p) {
        assert.equal(dst[p * 4 + 3], 255);
    }
    // The hot pixel is brighter than a cold one.
    assert.ok(dst[(2 * 8 + 3) * 4]! > dst[0]!);
});

test('drawCells paints rows x cols rects (the v8ui path)', () => {
    const h = new Heatmap();
    h.setImage(image(16, 32), 16, 32, -10);
    const r = new CountingRenderer(320, 160);
    h.drawCells(r, { x: 0, y: 0, w: 320, h: 160 });
    assert.equal(r.rects, 16 * 32);
    assert.equal(r.fills, 16 * 32);
});

test('overlays include the peak readout and DOA marker', () => {
    const h = new Heatmap();
    h.setImage(image(16, 32), 16, 32, -7.3);
    h.setEnergyVector({ x: 0, y: 1, z: 0 });
    const r = new CountingRenderer(320, 160);
    h.drawOverlays(r, { x: 0, y: 0, w: 320, h: 160 });
    assert.ok(r.texts.some((t) => t.includes('-7.3 dB')));
});
