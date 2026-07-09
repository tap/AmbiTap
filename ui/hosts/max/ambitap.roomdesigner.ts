/// Max v8ui host for the room designer. Bundled to
/// dist/max/ambitap.roomdesigner.js — load in a [v8ui] object.
///
/// Wiring (drives ambitap.room~, meters):
///   [v8ui @jsfile ambitap.roomdesigner.js] -> [ambitap.room~ 3]
/// The outlet emits room~'s geometry attribute messages on every edit:
/// `dim_x <m>` .. `dim_z`, `source_x` .. `source_z`, `listener_x` ..
/// `listener_z`. Incoming messages of the same names move the model
/// (drag-gated), and `rt60 <s>` / `reflections <6 floats>` update the
/// reflectogram overlay to match the object's settings.
///
/// The reflectogram and image cloud are drawn from core/imagesource.ts —
/// the same Allen-Berkley math dsp::room uses, cross-checked against the
/// C ABI by the test suite.
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { DEFAULT_WALL_REFLECTIONS, RoomDesigner, RoomParams } from '../../widgets/roomdesigner.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const room = new RoomDesigner({
    onChange: (p) => {
        outlet(0, 'dim_x', p.dims[0]);
        outlet(0, 'dim_y', p.dims[1]);
        outlet(0, 'dim_z', p.dims[2]);
        outlet(0, 'source_x', p.source[0]);
        outlet(0, 'source_y', p.source[1]);
        outlet(0, 'source_z', p.source[2]);
        outlet(0, 'listener_x', p.listener[0]);
        outlet(0, 'listener_y', p.listener[1]);
        outlet(0, 'listener_z', p.listener[2]);
    },
});

function paint(): void {
    room.draw(new MgraphicsRenderer(mgraphics));
}

function onclick(x: number, y: number): void {
    if (room.pointerDown(x, y, Date.now())) {
        mgraphics.redraw();
    }
}

function ondrag(x: number, y: number, button: number): void {
    const changed = button ? room.pointerMove(x, y, Date.now()) : room.pointerUp(Date.now());
    if (changed) {
        mgraphics.redraw();
    }
}

function onresize(): void {
    mgraphics.redraw();
}

// Incoming attribute mirrors (pattr / patch messages), drag-gated.
function setComponent(field: 'dims' | 'source' | 'listener', axis: number, value: number): void {
    const p: RoomParams = {
        dims: [...room.params.dims],
        source: [...room.params.source],
        listener: [...room.params.listener],
    };
    p[field][axis] = value;
    if (room.setParams(p, Date.now())) {
        mgraphics.redraw();
    }
}

g.dim_x = (v: number) => setComponent('dims', 0, v);
g.dim_y = (v: number) => setComponent('dims', 1, v);
g.dim_z = (v: number) => setComponent('dims', 2, v);
g.source_x = (v: number) => setComponent('source', 0, v);
g.source_y = (v: number) => setComponent('source', 1, v);
g.source_z = (v: number) => setComponent('source', 2, v);
g.listener_x = (v: number) => setComponent('listener', 0, v);
g.listener_y = (v: number) => setComponent('listener', 1, v);
g.listener_z = (v: number) => setComponent('listener', 2, v);

g.rt60 = (seconds: number) => {
    room.setRt60(seconds);
    mgraphics.redraw();
};
g.reflections = (...beta: number[]) => {
    if (beta.length === 6) {
        room.setWallReflections(beta as typeof DEFAULT_WALL_REFLECTIONS);
        mgraphics.redraw();
    } else {
        post('ambitap.roomdesigner: reflections wants 6 floats (x0 x1 y0 y1 z0 z1)\n');
    }
};

g.paint = paint;
g.onclick = onclick;
g.ondrag = ondrag;
g.onresize = onresize;
