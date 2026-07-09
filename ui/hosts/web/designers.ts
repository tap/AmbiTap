/// Browser host for the wave-4 designer widgets (UI.md). Parameter-heavy,
/// not frame-rate-heavy: the room reflectogram recomputes from the TS
/// image-source port on every drag; the XTC design (dsp::xtc in WASM)
/// reruns on release and the plot shows the design's own numbers — the
/// shipped FIRs and P = C·H against the KEMAR plant it inverted.

import { radians } from '../../core/coords.js';
import { CanvasRenderer } from '../../render/canvas.js';
import { RoomDesigner } from '../../widgets/roomdesigner.js';
import { XtcDesigner } from '../../widgets/xtcdesigner.js';
import { AmbitapModule, builtinHrtfHrir, XtcDesign } from './ambitap-module.js';

const DESIGN_RATE = 44100; // KEMAR dataset rate: P = C·H bins align exactly

function setupCanvas(id: string): { renderer: CanvasRenderer; canvas: HTMLCanvasElement } {
    const canvas = document.getElementById(id) as HTMLCanvasElement;
    const ctx = canvas.getContext('2d')!;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.width;
    const h = canvas.height;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    ctx.scale(dpr, dpr);
    return { renderer: new CanvasRenderer(ctx, w, h), canvas };
}

const roomView = setupCanvas('room');
const xtcView = setupCanvas('xtc');
const readout = document.getElementById('readout')!;

let dirty = true;

const room = new RoomDesigner({
    onChange: (p) => {
        dirty = true;
        readout.textContent =
            `room~  dims ${p.dims.map((v) => v.toFixed(2)).join(' × ')} m   ` +
            `source ${p.source.map((v) => v.toFixed(2)).join(', ')}   ` +
            `listener ${p.listener.map((v) => v.toFixed(2)).join(', ')}`;
    },
});

const xtc = new XtcDesigner({
    onChange: (p, phase) => {
        dirty = true;
        if (phase === 'end') {
            redesign(p.spanDeg, p.distance, p.regularization);
        }
    },
});

let design: XtcDesign | null = null;
let mod: AmbitapModule | null = null;

function redesign(spanDeg: number, distance: number, regularization: number): void {
    if (!design || !mod) {
        return;
    }
    design.design(spanDeg, distance, regularization, DESIGN_RATE);
    const info = design.info();
    xtc.setFilters(
        [
            [design.fir(0, 0), design.fir(0, 1)],
            [design.fir(1, 0), design.fir(1, 1)],
        ],
        DESIGN_RATE,
        { designGainDb: info.designGainDb, makeupLinear: info.makeupLinear, latencySamples: info.latencySamples },
    );
    // The plant the design inverted: LS KEMAR at ±span/2, elevation 0.
    const half = radians(spanDeg / 2);
    xtc.setPlant(
        builtinHrtfHrir(mod, 5, false, half, 0),
        builtinHrtfHrir(mod, 5, false, -half, 0),
    );
    const rejection = xtc.inBandRejectionDb();
    readout.textContent =
        `xtc~  span ${spanDeg.toFixed(1)}°  distance ${distance.toFixed(2)} m  ` +
        `regularization ${regularization.toFixed(2)}   design ${info.designGainDb.toFixed(1)} dB  ` +
        `latency ${((info.latencySamples / DESIGN_RATE) * 1000).toFixed(1)} ms` +
        (rejection !== null ? `   worst in-band rejection ${rejection.toFixed(1)} dB` : '');
    dirty = true;
}

async function boot(): Promise<void> {
    const bytes = await (await fetch('./ambitap.wasm')).arrayBuffer();
    mod = new AmbitapModule(await WebAssembly.compile(bytes));
    design = new XtcDesign(mod);
    redesign(xtc.params.spanDeg, xtc.params.distance, xtc.params.regularization);
}
boot().catch((err) => (readout.textContent = `wasm load failed: ${err}`));

interface PointerWidget {
    pointerDown(x: number, y: number, now: number): boolean;
    pointerMove(x: number, y: number, now: number): boolean;
    pointerUp(now: number): boolean;
}

function wirePointer(canvas: HTMLCanvasElement, widget: PointerWidget): void {
    const pos = (e: PointerEvent): { x: number; y: number } => {
        const rect = canvas.getBoundingClientRect();
        return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    };
    canvas.addEventListener('pointerdown', (e) => {
        const { x, y } = pos(e);
        if (widget.pointerDown(x, y, e.timeStamp)) {
            canvas.setPointerCapture(e.pointerId);
            dirty = true;
        }
    });
    canvas.addEventListener('pointermove', (e) => {
        const { x, y } = pos(e);
        dirty = widget.pointerMove(x, y, e.timeStamp) || dirty;
    });
    canvas.addEventListener('pointerup', (e) => {
        dirty = widget.pointerUp(e.timeStamp) || dirty;
    });
}

wirePointer(roomView.canvas, room);
wirePointer(xtcView.canvas, xtc);

function frame(): void {
    if (dirty) {
        dirty = false;
        room.draw(roomView.renderer);
        xtc.draw(xtcView.renderer);
    }
    requestAnimationFrame(frame);
}
frame();
