/// Browser host for the panner widget: canvas backend, pointer events,
/// repaint-on-dirty at display rate. Wave 2 replaces the readout with a WASM
/// AudioWorklet running the C ABI (see UI.md).

import { degrees } from '../../core/coords.js';
import { CanvasRenderer } from '../../render/canvas.js';
import { Panner, PannerSource } from '../../widgets/panner.js';

const canvas = document.getElementById('panner') as HTMLCanvasElement;
const readout = document.getElementById('readout') as HTMLElement;
const ctx = canvas.getContext('2d')!;

// Logical size for the model; device pixels for crispness.
const dpr = window.devicePixelRatio || 1;
const width = canvas.width;
const height = canvas.height;
canvas.style.width = `${width}px`;
canvas.style.height = `${height}px`;
canvas.width = Math.round(width * dpr);
canvas.height = Math.round(height * dpr);
ctx.scale(dpr, dpr);

const renderer = new CanvasRenderer(ctx, width, height);

let dirty = true;
const panner = new Panner({
    onChange: (source: PannerSource, phase) => {
        dirty = true;
        report(source, phase);
    },
});
panner.addSource('1', 0.0, 0.0);
panner.addSource('2', (-3 * Math.PI) / 4, Math.PI / 8);

function report(source: PannerSource, phase: string): void {
    // Radians on the wire (ambitap convention); degrees for the human.
    readout.textContent =
        `source ${source.id}  ` +
        `azimuth ${source.azimuth.toFixed(4)}  elevation ${source.elevation.toFixed(4)}  ` +
        `(${degrees(source.azimuth).toFixed(1)}°, ${degrees(source.elevation).toFixed(1)}°)  ${phase}`;
}

function pointerPos(e: PointerEvent): { x: number; y: number } {
    const rect = canvas.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
}

canvas.addEventListener('pointerdown', (e) => {
    const { x, y } = pointerPos(e);
    if (panner.pointerDown(x, y, e.timeStamp)) {
        canvas.setPointerCapture(e.pointerId);
        dirty = true;
    }
});
canvas.addEventListener('pointermove', (e) => {
    const { x, y } = pointerPos(e);
    dirty = panner.pointerMove(x, y, e.timeStamp) || dirty;
});
canvas.addEventListener('pointerup', (e) => {
    dirty = panner.pointerUp(e.timeStamp) || dirty;
});

function frame(): void {
    if (dirty) {
        dirty = false;
        panner.draw(renderer);
    }
    requestAnimationFrame(frame);
}
frame();

const first = panner.getSelected();
if (first) {
    report(first, 'init');
}
