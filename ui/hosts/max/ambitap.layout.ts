/// Max v8ui host for the decoder layout view. Bundled to
/// dist/max/ambitap.layout.js — load in a [v8ui] object.
///
/// Wiring:
///   `preset <name>` — one of the library layouts (stereo, quad, 5.1,
///     hexagon, 7.1, cube, octagon, 7.1.4), matching ambitap.decode~'s
///     layout argument. Or `speakers <az> <el> <az> <el> ...` (radians)
///     for a custom set.
///   [ambitap.decode~ 3 7.1.4] -> [mc.peakamp~ 50] -> [v8ui] — the list of
///     linear per-speaker amplitudes lights the dots.
///   Clicking a speaker sends `select <index> <azimuth> <elevation>` from
///     the outlet (route it to a solo/inspector).
///
/// NEEDS IN-MAX VERIFICATION (same status culture as the externals).

import { MgraphicsRenderer } from '../../render/mgraphics.js';
import { LAYOUT_PRESETS, SpeakerLayout } from '../../widgets/layout.js';

const g = globalThis as any;

g.inlets = 1;
g.outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

const view = new SpeakerLayout({
    onSelect: (index, speaker) => outlet(0, 'select', index, speaker.azimuth, speaker.elevation),
});
view.setPreset('stereo');

function paint(): void {
    view.draw(new MgraphicsRenderer(mgraphics));
}

function onclick(x: number, y: number): void {
    if (view.pointerDown(x, y)) {
        mgraphics.redraw();
    }
}

function preset(name: string): void {
    if (view.setPreset(String(name))) {
        mgraphics.redraw();
    } else {
        post(`ambitap.layout: unknown preset '${name}' (${Object.keys(LAYOUT_PRESETS).join(', ')})\n`);
    }
}

function speakers(...angles: number[]): void {
    if (angles.length < 2 || angles.length % 2 !== 0) {
        post('ambitap.layout: speakers message wants az/el pairs (radians)\n');
        return;
    }
    view.setSpeakers(
        Array.from({ length: angles.length / 2 }, (_, i) => ({
            label: `${i + 1}`,
            azimuth: angles[i * 2]!,
            elevation: angles[i * 2 + 1]!,
        })),
    );
    mgraphics.redraw();
}

function list(...amps: number[]): void {
    // mc.peakamp~ linear amplitudes -> dB.
    view.setLevels(amps.map((a) => 20 * Math.log10(Math.max(Math.abs(a), 1e-6))));
    mgraphics.redraw();
}

function onresize(): void {
    mgraphics.redraw();
}

g.paint = paint;
g.onclick = onclick;
g.preset = preset;
g.speakers = speakers;
g.list = list;
g.onresize = onresize;
