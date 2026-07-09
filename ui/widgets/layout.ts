/// Decoder layout view (UI.md widget catalog): speakers on the sphere in
/// the house top-down projection — front up, stage-left on screen-left,
/// elevation pulling dots toward the centre. Below-horizon speakers draw
/// hollow so the cube's lower ring is distinguishable from its upper one.
///
/// Read-mostly: click selects a speaker (hosts show its angles / route a
/// solo); per-speaker levels (dB) from the live decode feed size and warm
/// the dots. Presets mirror the library's math::layouts EXACTLY and are
/// cross-checked against ambitap_layout_preset by test/wasm.test.ts —
/// change them only with the library.

import { degrees, Direction, DiscFrame, topDownPoint } from '../core/coords.js';
import { Renderer, rgba } from '../render/renderer.js';

export interface Speaker extends Direction {
    label: string;
}

const DEG = Math.PI / 180;
const CUBE_EL = 35.2644 * DEG; // atan(1/sqrt(2)), layouts.h

function ring(labels: string[], azimuthsDeg: number[], elevationDeg = 0): Speaker[] {
    return labels.map((label, i) => ({
        label,
        azimuth: azimuthsDeg[i]! * DEG,
        elevation: elevationDeg * DEG,
    }));
}

/** Library speaker presets (math/geometry/layouts.h), library order. */
export const LAYOUT_PRESETS: Record<string, Speaker[]> = {
    stereo: ring(['L', 'R'], [30, -30]),
    quad: ring(['FL', 'BL', 'BR', 'FR'], [45, 135, -135, -45]),
    '5.1': ring(['L', 'R', 'C', 'LS', 'RS'], [30, -30, 0, 110, -110]),
    hexagon: ring(['1', '2', '3', '4', '5', '6'], [0, 60, 120, 180, -120, -60]),
    '7.1': ring(['L', 'R', 'C', 'LS', 'RS', 'LB', 'RB'], [30, -30, 0, 90, -90, 135, -135]),
    cube: [
        ...ring(['TFL', 'TFR', 'TBL', 'TBR'], [45, -45, 135, -135], 35.2644),
        ...ring(['BFL', 'BFR', 'BBL', 'BBR'], [45, -45, 135, -135], -35.2644),
    ],
    octagon: ring(['1', '2', '3', '4', '5', '6', '7', '8'], [0, 45, 90, 135, 180, 225, 270, 315]),
    '7.1.4': [
        ...ring(['L', 'R', 'C', 'LS', 'RS', 'LB', 'RB'], [30, -30, 0, 90, -90, 135, -135]),
        ...ring(['TFL', 'TFR', 'TBL', 'TBR'], [45, -45, 135, -135], 45),
    ],
};

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_DISC = rgba(0.18, 0.19, 0.22);
const COLOR_RIM = rgba(0.45, 0.47, 0.52);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.35);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const COLOR_COLD = rgba(0.35, 0.78, 0.98);
const COLOR_HOT = rgba(0.98, 0.76, 0.18);

const PAD = 8;
const GRAB_RADIUS = 14;
const LEVEL_FLOOR_DB = -60;

export interface SpeakerLayoutOptions {
    onSelect?: (index: number, speaker: Speaker) => void;
}

export class SpeakerLayout {
    private speakers: Speaker[] = [];
    private levelsDb: number[] | null = null;
    private selected = -1;
    private layoutCache: DiscFrame | null = null;
    private readonly onSelect: (index: number, speaker: Speaker) => void;

    constructor(options: SpeakerLayoutOptions = {}) {
        this.onSelect = options.onSelect ?? (() => {});
    }

    setPreset(name: string): boolean {
        const preset = LAYOUT_PRESETS[name];
        if (!preset) {
            return false;
        }
        this.setSpeakers(preset.map((s) => ({ ...s })));
        return true;
    }

    setSpeakers(speakers: Speaker[]): void {
        this.speakers = speakers;
        this.levelsDb = null;
        this.selected = -1;
    }

    getSpeakers(): readonly Speaker[] {
        return this.speakers;
    }

    /** Live per-speaker levels in dB (from the decode feed). */
    setLevels(levelsDb: ArrayLike<number> | null): void {
        this.levelsDb = levelsDb ? Array.from(levelsDb) : null;
    }

    selectedIndex(): number {
        return this.selected;
    }

    layout(width: number, height: number): DiscFrame {
        this.layoutCache = {
            cx: width / 2,
            cy: height / 2,
            radius: Math.max(Math.min(width, height) / 2 - PAD, 16),
        };
        return this.layoutCache;
    }

    /** Click to select the nearest speaker dot. */
    pointerDown(x: number, y: number): boolean {
        const disc = this.layoutCache;
        if (!disc) {
            return false;
        }
        let best = -1;
        let bestDist = GRAB_RADIUS;
        for (let i = 0; i < this.speakers.length; ++i) {
            const p = topDownPoint(this.speakers[i]!, disc);
            const d = Math.hypot(p.x - x, p.y - y);
            if (d < bestDist) {
                best = i;
                bestDist = d;
            }
        }
        if (best < 0) {
            return false;
        }
        this.selected = best;
        this.onSelect(best, this.speakers[best]!);
        return true;
    }

    draw(r: Renderer): void {
        const disc = this.layout(r.width, r.height);

        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();
        r.setColor(COLOR_DISC);
        r.arc(disc.cx, disc.cy, disc.radius, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_RIM);
        r.setLineWidth(1.5);
        r.arc(disc.cx, disc.cy, disc.radius, 0, Math.PI * 2);
        r.stroke();

        r.setColor(COLOR_GRID);
        r.setLineWidth(1);
        r.moveTo(disc.cx - disc.radius, disc.cy);
        r.lineTo(disc.cx + disc.radius, disc.cy);
        r.stroke();
        r.moveTo(disc.cx, disc.cy - disc.radius);
        r.lineTo(disc.cx, disc.cy + disc.radius);
        r.stroke();
        for (const el of [Math.PI / 6, Math.PI / 3]) {
            r.arc(disc.cx, disc.cy, disc.radius * Math.cos(el), 0, Math.PI * 2);
            r.stroke();
        }
        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        r.fillText('F', disc.cx - r.textWidth('F') / 2, disc.cy - disc.radius + 12);

        for (let i = 0; i < this.speakers.length; ++i) {
            this.drawSpeaker(r, i, disc);
        }

        if (this.selected >= 0 && this.selected < this.speakers.length) {
            const s = this.speakers[this.selected]!;
            r.setColor(COLOR_TEXT);
            r.setFontSize(10);
            r.fillText(
                `${s.label}  az ${degrees(s.azimuth).toFixed(1)}°  el ${degrees(s.elevation).toFixed(1)}°`,
                PAD,
                r.height - PAD,
            );
        }
    }

    private drawSpeaker(r: Renderer, index: number, disc: DiscFrame): void {
        const speaker = this.speakers[index]!;
        const p = topDownPoint(speaker, disc);

        // Level -> 0..1 warmth + size; no level feed -> neutral.
        let t = 0.5;
        const db = this.levelsDb?.[index];
        if (db !== undefined) {
            t = Math.min(1, Math.max(0, (db - LEVEL_FLOOR_DB) / -LEVEL_FLOOR_DB));
        }
        const color = rgba(
            COLOR_COLD.r + t * (COLOR_HOT.r - COLOR_COLD.r),
            COLOR_COLD.g + t * (COLOR_HOT.g - COLOR_COLD.g),
            COLOR_COLD.b + t * (COLOR_HOT.b - COLOR_COLD.b),
            1,
        );
        const radius = 4 + 4 * t;

        r.setColor(color);
        if (speaker.elevation >= 0) {
            r.arc(p.x, p.y, radius, 0, Math.PI * 2);
            r.fill();
        } else {
            // Below the horizon: hollow ring.
            r.setLineWidth(2);
            r.arc(p.x, p.y, radius, 0, Math.PI * 2);
            r.stroke();
        }
        if (index === this.selected) {
            r.setLineWidth(1.5);
            r.arc(p.x, p.y, radius + 3.5, 0, Math.PI * 2);
            r.stroke();
        }
        r.setColor(COLOR_TEXT);
        r.setFontSize(8);
        r.fillText(speaker.label, p.x + radius + 2, p.y - 2);
    }
}
