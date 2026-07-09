/// Max v8ui host for the per-ACN-channel meter bridge. Bundled to
/// dist/max/ambitap.meters.js — load in a [v8ui] object.
///
/// Wiring (per-channel peak amplitudes of the mc HOA bus at display rate):
///   [mc. HOA bus] -> [mc.peakamp~ 50] -> [v8ui @jsfile ambitap.meters.js]
/// Accepts a list of LINEAR amplitudes (mc.peakamp~'s output) and converts
/// to dB for display.
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { Meters } from '../../widgets/meters.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 0;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const meters = new Meters();

function paint(): void {
    meters.draw(new MgraphicsRenderer(mgraphics));
}

function list(...amps: number[]): void {
    meters.update(amps.map((a) => 20 * Math.log10(Math.max(Math.abs(a), 1e-6))));
    mgraphics.redraw();
}

function onresize(): void {
    mgraphics.redraw();
}

g.paint = paint;
g.list = list;
g.onresize = onresize;
