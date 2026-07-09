/// Max v8ui host for the soundfield heatmap. Bundled to
/// dist/max/ambitap.heatmap.js — load in a [v8ui] object.
///
/// Wiring:
///   [mc. HOA bus] -> [ambitap.grid~ 3] -> right outlet -> [v8ui @jsfile ambitap.heatmap.js]
///   [qmetro 33] -> [ambitap.grid~] (bang drives snapshots at display rate)
/// The external's `grid <rows> <cols> <peak_db> <values...>` message paints
/// directly; optional `vec <x> <y> <z>` (from ambitap.energyvec~ via
/// [snapshot~]) overlays the energy-vector marker, and
/// `marker <id> <azimuth> <elevation>` / `clearmarkers` manage source dots.
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { Heatmap } from '../../widgets/heatmap.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 0;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const heatmap = new Heatmap();

function paint(): void {
    const r = new MgraphicsRenderer(mgraphics);
    const frame = { x: 0, y: 0, w: r.width, h: r.height };
    heatmap.drawCells(r, frame); // dependency-free image path (no Jitter)
    heatmap.drawOverlays(r, frame);
}

function grid(...args: number[]): void {
    const [rows, cols, peakDb, ...values] = args;
    if (!rows || !cols || values.length !== rows * cols) {
        post(`ambitap.heatmap: bad grid message (${args.length} atoms)\n`);
        return;
    }
    heatmap.setImage(Float32Array.from(values), rows, cols, peakDb ?? 0);
    mgraphics.redraw();
}

function vec(x: number, y: number, z: number): void {
    heatmap.setEnergyVector({ x, y, z });
    mgraphics.redraw();
}

function marker(id: number | string, azimuth: number, elevation: number): void {
    const key = String(id);
    heatmap.setMarkers([
        ...heatmap.markers.filter((m) => m.id !== key),
        { id: key, azimuth, elevation },
    ]);
    mgraphics.redraw();
}

function clearmarkers(): void {
    heatmap.setMarkers([]);
    mgraphics.redraw();
}

function onresize(): void {
    mgraphics.redraw();
}

g.paint = paint;
g.grid = grid;
g.vec = vec;
g.marker = marker;
g.clearmarkers = clearmarkers;
g.onresize = onresize;
