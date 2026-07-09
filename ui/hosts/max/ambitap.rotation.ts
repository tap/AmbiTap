/// Max v8ui host for the rotation / head-orientation ball. Bundled to
/// dist/max/ambitap.rotation.js — load in a [v8ui] object.
///
/// Wiring (drives ambitap.rotate~ or ambitap.binaural~, radians):
///   [v8ui @jsfile ambitap.rotation.js] -> [ambitap.rotate~ 3]
/// The outlet emits `yaw <rad>`, `pitch <rad>`, `roll <rad>` messages.
/// Incoming `yaw <f>` / `pitch <f>` / `roll <f>` / `ypr <y> <p> <r>`
/// messages move the ball (OSC head-tracking display: [udpreceive] ->
/// [route /ambitap/orientation] -> [unpack 0. 0. 0.] -> [pak ypr 0. 0. 0.]),
/// gated while the user drags. `reset` restores identity.
///
/// Drag inside the ball to tumble; drag the outer ring for yaw-only.
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { RotationBall } from '../../widgets/rotation.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const ball = new RotationBall({
    onChange: (ypr) => {
        outlet(0, 'yaw', ypr.yaw);
        outlet(0, 'pitch', ypr.pitch);
        outlet(0, 'roll', ypr.roll);
    },
});

// Incoming values accumulate here so single-angle messages compose.
let pending = { yaw: 0, pitch: 0, roll: 0 };

function paint(): void {
    ball.draw(new MgraphicsRenderer(mgraphics));
}

function onclick(x: number, y: number): void {
    if (ball.pointerDown(x, y, Date.now())) {
        mgraphics.redraw();
    }
}

function ondrag(x: number, y: number, button: number): void {
    const changed = button ? ball.pointerMove(x, y, Date.now()) : ball.pointerUp(Date.now());
    if (changed) {
        mgraphics.redraw();
    }
}

function ondblclick(): void {
    ball.reset();
    mgraphics.redraw();
}

function onresize(): void {
    mgraphics.redraw();
}

function applyPending(): void {
    if (ball.setYawPitchRoll(pending.yaw, pending.pitch, pending.roll, Date.now())) {
        mgraphics.redraw();
    }
}

function yaw(v: number): void {
    pending.yaw = v;
    applyPending();
}
function pitch(v: number): void {
    pending.pitch = v;
    applyPending();
}
function roll(v: number): void {
    pending.roll = v;
    applyPending();
}
function ypr(y: number, p: number, r: number): void {
    pending = { yaw: y, pitch: p, roll: r };
    applyPending();
}
function reset(): void {
    pending = { yaw: 0, pitch: 0, roll: 0 };
    ball.reset();
    mgraphics.redraw();
}

g.paint = paint;
g.onclick = onclick;
g.ondrag = ondrag;
g.ondblclick = ondblclick;
g.onresize = onresize;
g.yaw = yaw;
g.pitch = pitch;
g.roll = roll;
g.ypr = ypr;
g.reset = reset;
