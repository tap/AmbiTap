/// Max v8ui host for the XTC designer. Bundled to
/// dist/max/ambitap.xtcdesigner.js — load in a [v8ui] object.
///
/// Wiring (drives ambitap.xtc~):
///   [v8ui @jsfile ambitap.xtcdesigner.js] -> [ambitap.xtc~]
/// The outlet emits `span <deg>`, `distance <m>`, `regularization <0..1>`
/// on edits; the same messages back move the model (drag-gated).
///
/// Filter plot: send xtc~ the `dumpfir` message and route its dump to this
/// v8ui — `firinfo <sample_rate> <latency> <design_gain_db> <makeup>` then
/// four `fir <speaker> <input> <taps...>` lists. Without a dump the widget
/// shows geometry only. (The P = C·H prediction needs the KEMAR plant and
/// currently lives in the browser designer, which reads it from the WASM
/// ABI.)
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { XtcDesigner } from '../../widgets/xtcdesigner.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const xtc = new XtcDesigner({
    onChange: (p) => {
        outlet(0, 'span', p.spanDeg);
        outlet(0, 'distance', p.distance);
        outlet(0, 'regularization', p.regularization);
    },
});

// FIR dump assembly: firinfo arrives first, then the four fir lists.
const pendingFirs: Float32Array[][] = [[], []] as unknown as Float32Array[][];
let pendingInfo = { sampleRate: 44100, latencySamples: 512, designGainDb: 0, makeupLinear: 1 };
let pendingCount = 0;

function paint(): void {
    xtc.draw(new MgraphicsRenderer(mgraphics));
}

function onclick(x: number, y: number): void {
    if (xtc.pointerDown(x, y, Date.now())) {
        mgraphics.redraw();
    }
}

function ondrag(x: number, y: number, button: number): void {
    const changed = button ? xtc.pointerMove(x, y, Date.now()) : xtc.pointerUp(Date.now());
    if (changed) {
        mgraphics.redraw();
    }
}

function onresize(): void {
    mgraphics.redraw();
}

function span(v: number): void {
    if (xtc.setParams({ ...xtc.params, spanDeg: v }, Date.now())) {
        mgraphics.redraw();
    }
}
function distance(v: number): void {
    if (xtc.setParams({ ...xtc.params, distance: v }, Date.now())) {
        mgraphics.redraw();
    }
}
function regularization(v: number): void {
    if (xtc.setParams({ ...xtc.params, regularization: v }, Date.now())) {
        mgraphics.redraw();
    }
}

function firinfo(sampleRate: number, latencySamples: number, designGainDb: number, makeupLinear: number): void {
    pendingInfo = { sampleRate, latencySamples, designGainDb, makeupLinear };
    pendingCount = 0;
}

function fir(speaker: number, input: number, ...taps: number[]): void {
    if (speaker < 0 || speaker > 1 || input < 0 || input > 1 || taps.length === 0) {
        post('ambitap.xtcdesigner: bad fir message\n');
        return;
    }
    if (!pendingFirs[speaker]) {
        pendingFirs[speaker] = [];
    }
    pendingFirs[speaker]![input] = Float32Array.from(taps);
    if (++pendingCount >= 4) {
        xtc.setFilters(
            pendingFirs as [[Float32Array, Float32Array], [Float32Array, Float32Array]],
            pendingInfo.sampleRate,
            {
                designGainDb: pendingInfo.designGainDb,
                makeupLinear: pendingInfo.makeupLinear,
                latencySamples: pendingInfo.latencySamples,
            },
        );
        pendingCount = 0;
        mgraphics.redraw();
    }
}

g.paint = paint;
g.onclick = onclick;
g.ondrag = ondrag;
g.onresize = onresize;
g.span = span;
g.distance = distance;
g.regularization = regularization;
g.firinfo = firinfo;
g.fir = fir;
