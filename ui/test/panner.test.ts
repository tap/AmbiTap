import assert from 'node:assert/strict';
import { test } from 'node:test';
import { degrees } from '../core/coords.js';
import { Renderer, RGBA } from '../render/renderer.js';
import { Panner, PannerPhase, PannerSource } from '../widgets/panner.js';

/** Records draw calls; proves the widget never needs a host API. */
class StubRenderer implements Renderer {
    calls: string[] = [];
    constructor(
        public readonly width: number,
        public readonly height: number,
    ) {}
    private log(name: string): void {
        this.calls.push(name);
    }
    save(): void { this.log('save'); }
    restore(): void { this.log('restore'); }
    beginPath(): void { this.log('beginPath'); }
    moveTo(): void { this.log('moveTo'); }
    lineTo(): void { this.log('lineTo'); }
    arc(): void { this.log('arc'); }
    rect(): void { this.log('rect'); }
    closePath(): void { this.log('closePath'); }
    setColor(_c: RGBA): void { this.log('setColor'); }
    setLineWidth(): void { this.log('setLineWidth'); }
    fill(): void { this.log('fill'); }
    stroke(): void { this.log('stroke'); }
    setFontSize(): void { this.log('setFontSize'); }
    fillText(): void { this.log('fillText'); }
    textWidth(text: string): number { return text.length * 5; }
}

function makePanner(): {
    panner: Panner;
    events: Array<{ id: string; azimuth: number; elevation: number; phase: PannerPhase }>;
} {
    const events: Array<{ id: string; azimuth: number; elevation: number; phase: PannerPhase }> = [];
    const panner = new Panner({
        onChange: (s: PannerSource, phase) =>
            events.push({ id: s.id, azimuth: s.azimuth, elevation: s.elevation, phase }),
    });
    panner.addSource('1', 0, 0);
    panner.draw(new StubRenderer(300, 300)); // establishes layout for hit-testing
    return { panner, events };
}

test('draw renders through the Renderer interface alone', () => {
    const { panner } = makePanner();
    const r = new StubRenderer(300, 300);
    panner.draw(r);
    assert.ok(r.calls.includes('arc'));
    assert.ok(r.calls.includes('fillText'));
});

// Layout for 300x300: centre (150,150), ring radius 135, disc radius 117.

test('dragging on the disc sets azimuth only', () => {
    const { panner, events } = makePanner();
    assert.ok(panner.pointerDown(150, 150 - 60, 1000)); // above centre = front
    assert.ok(panner.pointerMove(150 - 60, 150, 1010)); // screen-left = +pi/2
    panner.pointerUp(1020);

    const last = events[events.length - 1]!;
    assert.equal(last.phase, 'end');
    assert.ok(Math.abs(degrees(last.azimuth) - 90) < 1e-6, `az ${degrees(last.azimuth)}`);
    assert.equal(last.elevation, 0);
});

test('dragging the right-hand ring sets elevation, top = zenith', () => {
    const { panner, events } = makePanner();
    // Point on the ring band, 45 deg up-right of centre: elevation +45 deg.
    const d = 135 / Math.SQRT2;
    assert.ok(panner.pointerDown(150 + d, 150 - d, 1000));
    panner.pointerUp(1010);

    const last = events[events.length - 1]!;
    assert.ok(Math.abs(degrees(last.elevation) - 45) < 1e-6, `el ${degrees(last.elevation)}`);
    assert.equal(last.azimuth, 0); // untouched
});

test('external setDirection applies when idle, is gated during and just after a drag', () => {
    const { panner } = makePanner();
    assert.ok(panner.setDirection('1', 1.0, 0.5, 1000));
    const s = panner.getSource('1')!;
    assert.equal(s.azimuth, 1.0);

    panner.pointerDown(150, 100, 2000);
    assert.equal(panner.setDirection('1', -2.0, 0, 2005), false); // dragging
    panner.pointerUp(2010);
    assert.equal(panner.setDirection('1', -2.0, 0, 2100), false); // inside hold window
    assert.ok(panner.setDirection('1', -2.0, 0, 2500)); // window elapsed
    assert.equal(s.azimuth, -2.0);
});

test('pointerDown selects the nearest source dot', () => {
    const { panner } = makePanner();
    panner.addSource('2', Math.PI, 0); // behind: bottom of the disc
    panner.draw(new StubRenderer(300, 300));
    assert.ok(panner.pointerDown(150, 150 + 117, 1000)); // on source 2's dot
    assert.equal(panner.getSelected()!.id, '2');
    panner.pointerUp(1001);
});

test('events outside the widget are not consumed', () => {
    const { panner } = makePanner();
    assert.equal(panner.pointerDown(2, 2, 1000), false); // corner: outside disc+ring
    assert.equal(panner.isDragging(), false);
});
