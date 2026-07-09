import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Renderer, RGBA } from '../render/renderer.js';
import { LAYOUT_PRESETS, SpeakerLayout } from '../widgets/layout.js';

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

test('preset table shapes match the decode~ layout set', () => {
    assert.equal(LAYOUT_PRESETS['stereo']!.length, 2);
    assert.equal(LAYOUT_PRESETS['quad']!.length, 4);
    assert.equal(LAYOUT_PRESETS['5.1']!.length, 5);
    assert.equal(LAYOUT_PRESETS['hexagon']!.length, 6);
    assert.equal(LAYOUT_PRESETS['7.1']!.length, 7);
    assert.equal(LAYOUT_PRESETS['cube']!.length, 8);
    assert.equal(LAYOUT_PRESETS['octagon']!.length, 8);
    assert.equal(LAYOUT_PRESETS['7.1.4']!.length, 11);
});

test('selection: click near a speaker dot selects it and fires onSelect', () => {
    let selected: { index: number; label: string } | null = null;
    const view = new SpeakerLayout({ onSelect: (index, s) => (selected = { index, label: s.label }) });
    view.setPreset('stereo');
    view.draw(new StubRenderer(200, 200)); // disc centre (100,100), radius 92

    // L at az +30 deg, el 0: x = 100 - 92*sin(30deg) = 54, y = 100 - 92*cos(30deg).
    const x = 100 - 92 * Math.sin(Math.PI / 6);
    const y = 100 - 92 * Math.cos(Math.PI / 6);
    assert.ok(view.pointerDown(x, y));
    assert.equal(view.selectedIndex(), 0);
    assert.deepEqual(selected, { index: 0, label: 'L' });

    assert.equal(view.pointerDown(100, 100), false); // centre: nothing there
});

test('setPreset resets levels and selection; unknown preset refused', () => {
    const view = new SpeakerLayout();
    assert.ok(view.setPreset('7.1.4'));
    view.setLevels(new Array(11).fill(-12));
    assert.equal(view.setPreset('atmos-99'), false);
    assert.ok(view.setPreset('quad'));
    assert.equal(view.selectedIndex(), -1);
    assert.equal(view.getSpeakers().length, 4);
});
