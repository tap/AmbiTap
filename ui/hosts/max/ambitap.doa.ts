/// Max v8ui host for the energy-vector DOA dot. Bundled to
/// dist/max/ambitap.doa.js — load in a [v8ui] object.
///
/// Wiring (x/y/z signals from ambitap.energyvec~, sampled at display rate):
///   [ambitap.energyvec~] -> [snapshot~ 33] x3 -> [pak vec 0. 0. 0.] -> [v8ui]
/// Accepts `vec <x> <y> <z>` or a raw list `<x> <y> <z>`.
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { Doa } from '../../widgets/doa.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 0;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const doa = new Doa();

function paint(): void {
    doa.draw(new MgraphicsRenderer(mgraphics));
}

function vec(x: number, y: number, z: number): void {
    doa.update({ x, y, z });
    mgraphics.redraw();
}

function list(x: number, y: number, z: number): void {
    vec(x, y, z);
}

function onresize(): void {
    mgraphics.redraw();
}

g.paint = paint;
g.vec = vec;
g.list = list;
g.onresize = onresize;
