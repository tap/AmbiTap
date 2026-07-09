import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Renderer, RGBA } from '../render/renderer.js';
import { defaultRoomParams, RoomDesigner } from '../widgets/roomdesigner.js';

class StubRenderer implements Renderer {
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
    rect(): void {}
    closePath(): void {}
    setColor(_c: RGBA): void {}
    setLineWidth(): void {}
    fill(): void {}
    stroke(): void {}
    setFontSize(): void {}
    fillText(): void {}
    textWidth(text: string): number {
        return text.length * 5;
    }
}

const W = 680;
const H = 380;

function makeRoom(): {
    room: RoomDesigner;
    events: Array<{ phase: string }>;
    planPoint: (x: number, y: number) => { x: number; y: number };
} {
    const events: Array<{ phase: string }> = [];
    const room = new RoomDesigner({ onChange: (_p, phase) => events.push({ phase }) });
    room.draw(new StubRenderer(W, H));
    // Recreate the plan mapping (widget internals: pane + centred 2.2x fit).
    const panes = room.layout(W, H);
    const planPoint = (x: number, y: number): { x: number; y: number } => {
        const [lx, ly] = room.params.dims;
        const s = Math.min(panes.plan.w / (2.2 * ly!), panes.plan.h / (2.2 * lx!));
        const ox = panes.plan.x + (panes.plan.w - ly! * s) / 2;
        const oy = panes.plan.y + (panes.plan.h - lx! * s) / 2;
        return { x: ox + (ly! - y) * s, y: oy + (lx! - x) * s };
    };
    return { room, events, planPoint };
}

test('dragging the source in plan moves x/y and leaves z alone', () => {
    const { room, events, planPoint } = makeRoom();
    const start = planPoint(room.params.source[0], room.params.source[1]);
    assert.ok(room.pointerDown(start.x, start.y, 1000));

    const target = planPoint(5.0, 2.0);
    room.pointerMove(target.x, target.y, 1010);
    room.pointerUp(1020);

    assert.ok(Math.abs(room.params.source[0] - 5.0) < 1e-6, `x ${room.params.source[0]}`);
    assert.ok(Math.abs(room.params.source[1] - 2.0) < 1e-6, `y ${room.params.source[1]}`);
    assert.ok(Math.abs(room.params.source[2] - defaultRoomParams().source[2]) < 1e-9);
    assert.equal(events[events.length - 1]!.phase, 'end');
});

test('positions clamp inside the room', () => {
    const { room, planPoint } = makeRoom();
    const start = planPoint(room.params.listener[0], room.params.listener[1]);
    assert.ok(room.pointerDown(start.x, start.y, 1000));
    const outside = planPoint(100, -50);
    room.pointerMove(outside.x, outside.y, 1010);
    room.pointerUp(1020);
    assert.ok(room.params.listener[0] <= room.params.dims[0] - 0.099);
    assert.ok(room.params.listener[1] >= 0.099);
});

test('dragging the front wall (x = Lx) resizes dim x', () => {
    const { room, planPoint } = makeRoom();
    const before = room.params.dims[0];
    const wall = planPoint(room.params.dims[0], room.params.dims[1] / 2);
    assert.ok(room.pointerDown(wall.x, wall.y, 1000), 'wall grabbed');
    room.pointerMove(wall.x, wall.y - 30, 1010); // outward = larger room
    room.pointerUp(1020);
    assert.ok(room.params.dims[0] > before, `dims.x ${room.params.dims[0]} > ${before}`);
});

test('external setParams is gated during a drag', () => {
    const { room, planPoint } = makeRoom();
    const start = planPoint(room.params.source[0], room.params.source[1]);
    room.pointerDown(start.x, start.y, 1000);
    const p = defaultRoomParams();
    p.dims = [4, 4, 4];
    assert.equal(room.setParams(p, 1001), false);
    room.pointerUp(1002);
    assert.ok(room.setParams(p, 2000));
    assert.equal(room.params.dims[0], 4);
});

test('image set has one direct arrival and refreshes after edits', () => {
    const { room, planPoint } = makeRoom();
    const before = room.imageSet();
    assert.equal(before.filter((i) => i.reflections === 0).length, 1);

    const start = planPoint(room.params.source[0], room.params.source[1]);
    room.pointerDown(start.x, start.y, 1000);
    const target = planPoint(1.0, 4.5);
    room.pointerMove(target.x, target.y, 1010);
    room.pointerUp(1020);

    const after = room.imageSet();
    const directBefore = before.find((i) => i.reflections === 0)!;
    const directAfter = after.find((i) => i.reflections === 0)!;
    assert.notEqual(directBefore.t, directAfter.t, 'direct arrival moved with the source');
});
