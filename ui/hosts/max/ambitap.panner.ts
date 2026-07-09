/// Max v8ui host for the panner widget. Bundled by scripts/build.mjs to
/// dist/max/ambitap.panner.js — load that file in a [v8ui] object.
///
/// Wiring (single source, matching ambitap.encode~):
///   [v8ui @jsfile ambitap.panner.js] -> [ambitap.encode~ 3]
/// The outlet emits `azimuth <radians>` and `elevation <radians>` messages
/// that connect straight to the encode~ inlet. Incoming `azimuth <f>` /
/// `elevation <f>` messages move the dot (bidirectional; ignored while the
/// user is dragging).
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals): the
/// mgraphics/v8ui event surface here is written from the documented API and
/// has not yet been exercised in a running Max.

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { Panner } from '../../widgets/panner.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const panner = new Panner({
    onChange: (source, _phase) => {
        outlet(0, 'azimuth', source.azimuth);
        outlet(0, 'elevation', source.elevation);
    },
});
panner.addSource('1', 0, 0);

function paint(): void {
    panner.draw(new MgraphicsRenderer(mgraphics));
}

// jsui/v8ui mouse protocol: onclick on press, ondrag while dragging and
// once more with button 0 on release.
function onclick(x: number, y: number): void {
    if (panner.pointerDown(x, y, Date.now())) {
        mgraphics.redraw();
    }
}

function ondrag(x: number, y: number, button: number): void {
    const changed = button ? panner.pointerMove(x, y, Date.now()) : panner.pointerUp(Date.now());
    if (changed) {
        mgraphics.redraw();
    }
}

function onresize(): void {
    mgraphics.redraw();
}

// Bidirectional control: reflect externally-set values (patch messages,
// pattr, OSC via [udpreceive] -> [route azimuth elevation]).
function azimuth(value: number): void {
    const s = panner.getSelected();
    if (s && panner.setDirection(s.id, value, s.elevation, Date.now())) {
        mgraphics.redraw();
    }
}

function elevation(value: number): void {
    const s = panner.getSelected();
    if (s && panner.setDirection(s.id, s.azimuth, value, Date.now())) {
        mgraphics.redraw();
    }
}

// v8ui resolves handlers as globals; the esbuild IIFE wrapper would
// otherwise keep them module-local.
g.paint = paint;
g.onclick = onclick;
g.ondrag = ondrag;
g.onresize = onresize;
g.azimuth = azimuth;
g.elevation = elevation;
