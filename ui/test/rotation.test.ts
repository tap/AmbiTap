import assert from 'node:assert/strict';
import { test } from 'node:test';
import { degrees, HALF_PI } from '../core/coords.js';
import { Renderer, RGBA } from '../render/renderer.js';
import { RotationBall } from '../widgets/rotation.js';

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

function makeBall(): {
    ball: RotationBall;
    events: Array<{ yaw: number; pitch: number; roll: number; phase: string }>;
} {
    const events: Array<{ yaw: number; pitch: number; roll: number; phase: string }> = [];
    const ball = new RotationBall({ onChange: (ypr, phase) => events.push({ ...ypr, phase }) });
    ball.draw(new StubRenderer(200, 200)); // layout: centre (100,100), ring 92, ball 78
    return { ball, events };
}

test('starts at identity', () => {
    const { ball } = makeBall();
    const ypr = ball.yawPitchRoll();
    assert.equal(ypr.yaw, 0);
    assert.equal(ypr.pitch, 0);
    assert.equal(ypr.roll, 0);
});

test('ring drag twists yaw only: counterclockwise (seen from above) = +yaw', () => {
    const { ball, events } = makeBall();
    // Start on the ring at screen-right, drag a quarter turn to screen-top
    // (counterclockwise on screen).
    assert.ok(ball.pointerDown(100 + 92, 100, 1000));
    ball.pointerMove(100, 100 - 92, 1001);
    ball.pointerUp(1002);
    const last = events[events.length - 1]!;
    assert.ok(Math.abs(degrees(last.yaw) - 90) < 1e-6, `yaw ${degrees(last.yaw)}`);
    assert.ok(Math.abs(last.pitch) < 1e-6);
    assert.ok(Math.abs(last.roll) < 1e-6);
});

test('tumble drag from centre toward screen-top pitches the front DOWN (+pitch)', () => {
    const { ball, events } = makeBall();
    assert.ok(ball.pointerDown(100, 100, 1000)); // ball centre
    ball.pointerMove(100, 100 - 78, 1001); // to the top of the ball
    ball.pointerUp(1002);
    const last = events[events.length - 1]!;
    assert.ok(Math.abs(degrees(last.pitch) - 90) < 1e-4, `pitch ${degrees(last.pitch)}`);
});

test('external setYawPitchRoll applies when idle and is gated during drags', () => {
    const { ball } = makeBall();
    assert.ok(ball.setYawPitchRoll(HALF_PI, 0.2, -0.1, 500));
    let ypr = ball.yawPitchRoll();
    assert.ok(Math.abs(ypr.yaw - HALF_PI) < 1e-6);
    assert.ok(Math.abs(ypr.pitch - 0.2) < 1e-6);
    assert.ok(Math.abs(ypr.roll - -0.1) < 1e-6);

    ball.pointerDown(100, 100, 1000);
    assert.equal(ball.setYawPitchRoll(0, 0, 0, 1001), false);
    ball.pointerUp(1002);
    assert.equal(ball.setYawPitchRoll(0, 0, 0, 1100), false); // hold window
    assert.ok(ball.setYawPitchRoll(0, 0, 0, 1500));
    ypr = ball.yawPitchRoll();
    assert.ok(Math.abs(ypr.yaw) < 1e-6);
});

test('reset returns to identity and reports it', () => {
    const { ball, events } = makeBall();
    ball.setYawPitchRoll(1, 0.5, -0.5, 0);
    ball.reset();
    const last = events[events.length - 1]!;
    assert.equal(last.phase, 'end');
    assert.ok(Math.abs(last.yaw) < 1e-9 && Math.abs(last.pitch) < 1e-9 && Math.abs(last.roll) < 1e-9);
});

test('presses outside the ring are not consumed', () => {
    const { ball } = makeBall();
    assert.equal(ball.pointerDown(2, 2, 0), false);
    assert.equal(ball.isDragging(), false);
});
