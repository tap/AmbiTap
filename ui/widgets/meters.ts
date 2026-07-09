/// Per-ACN-channel meter bridge: one bar per HOA channel with peak hold —
/// 16 bars at order 3, 36 at order 5. Diagnostic, cheap, and the fastest
/// way to see bus wiring problems (UI.md widget catalog).

import { Renderer, rgba } from '../render/renderer.js';

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_WELL = rgba(0.18, 0.19, 0.22);
const COLOR_BAR = rgba(0.35, 0.78, 0.98);
const COLOR_HOT = rgba(0.98, 0.76, 0.18);
const COLOR_PEAK = rgba(0.9, 0.92, 0.95);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.3);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);

const FLOOR_DB = -60;
const CEIL_DB = 0;
const HOT_DB = -12; // bar turns amber above this
const PEAK_DECAY_DB = 0.35; // per update() call

export class Meters {
    private levels: number[] = [];
    private peaks: number[] = [];

    /** Latest per-channel levels in dB (ACN order). */
    update(levelsDb: ArrayLike<number>): void {
        if (this.levels.length !== levelsDb.length) {
            this.levels = new Array<number>(levelsDb.length).fill(FLOOR_DB);
            this.peaks = new Array<number>(levelsDb.length).fill(FLOOR_DB);
        }
        for (let i = 0; i < levelsDb.length; ++i) {
            const db = Math.min(CEIL_DB, Math.max(FLOOR_DB, levelsDb[i]!));
            this.levels[i] = db;
            this.peaks[i] = Math.max(db, this.peaks[i]! - PEAK_DECAY_DB);
        }
    }

    channelCount(): number {
        return this.levels.length;
    }

    /** Bar-top y for a level, within a well of the given geometry. */
    static levelToY(db: number, top: number, height: number): number {
        const clamped = Math.min(CEIL_DB, Math.max(FLOOR_DB, db));
        return top + ((CEIL_DB - clamped) / (CEIL_DB - FLOOR_DB)) * height;
    }

    draw(r: Renderer): void {
        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();
        if (this.levels.length === 0) {
            return;
        }

        const padL = 22; // dB scale gutter
        const padR = 4;
        const padT = 4;
        const padB = 14; // ACN labels
        const wellW = (r.width - padL - padR) / this.levels.length;
        const barW = Math.max(wellW - 2, 1);
        const wellH = r.height - padT - padB;

        // dB grid + scale.
        r.setFontSize(8);
        for (const db of [0, -20, -40, -60]) {
            const y = Meters.levelToY(db, padT, wellH);
            r.setColor(COLOR_GRID);
            r.setLineWidth(1);
            r.moveTo(padL, y);
            r.lineTo(r.width - padR, y);
            r.stroke();
            r.setColor(COLOR_TEXT);
            r.fillText(`${db}`, 2, y + 3);
        }

        const labelEvery = wellW >= 13 ? 1 : wellW >= 7 ? 4 : 8;
        for (let i = 0; i < this.levels.length; ++i) {
            const x = padL + i * wellW + (wellW - barW) / 2;
            r.setColor(COLOR_WELL);
            r.rect(x, padT, barW, wellH);
            r.fill();

            const level = this.levels[i]!;
            const y = Meters.levelToY(level, padT, wellH);
            r.setColor(level > HOT_DB ? COLOR_HOT : COLOR_BAR);
            r.rect(x, y, barW, padT + wellH - y);
            r.fill();

            const peakY = Meters.levelToY(this.peaks[i]!, padT, wellH);
            r.setColor(COLOR_PEAK);
            r.rect(x, peakY, barW, 1.5);
            r.fill();

            if (i % labelEvery === 0) {
                r.setColor(COLOR_TEXT);
                r.setFontSize(8);
                const label = `${i}`;
                r.fillText(label, x + barW / 2 - r.textWidth(label) / 2, r.height - 4);
            }
        }
    }
}
