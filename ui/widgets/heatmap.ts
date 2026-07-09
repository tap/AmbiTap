/// Soundfield energy heatmap on the analysis::soundfield_grid layout:
/// equirectangular, row 0 = zenith, column 0 = azimuth -pi (back wrap),
/// centre column = front. Values arrive normalized [0, 1] over a dynamic
/// range below the peak (grid snapshot / ambitap_grid_snapshot).
///
/// Two display paths, per UI.md:
/// - writeRGBA(): fill an RGBA pixel buffer for the host's native image blit
///   (browser ImageData; a future jit.matrix path uses the same colormap).
/// - drawCells(): paint rows x cols rects through the Renderer — the
///   dependency-free v8ui path, fine at az_steps <= 32.
/// drawOverlays() adds the graticule, cardinal labels, source markers, the
/// energy-vector marker, and the peak-dB readout on either path.

import {
    clampElevation,
    Direction,
    equirectOfDirection,
    toSpherical,
    Vec3,
    wrapAzimuth,
} from '../core/coords.js';
import { Renderer, RGBA, rgba } from '../render/renderer.js';

export interface HeatmapRect {
    x: number;
    y: number;
    w: number;
    h: number;
}

export interface HeatmapMarker extends Direction {
    id: string;
    color?: RGBA;
}

const COLOR_GRID = rgba(1, 1, 1, 0.22);
const COLOR_TEXT = rgba(0.85, 0.87, 0.9, 0.9);
const COLOR_DOA = rgba(1, 1, 1, 0.95);

/** Inferno-like ramp, 0 = near-black, 1 = near-white; stops chosen to stay
 *  legible on the dark widget background. */
const RAMP: Array<[number, number, number, number]> = [
    [0.0, 0.05, 0.04, 0.1],
    [0.25, 0.25, 0.06, 0.36],
    [0.5, 0.72, 0.16, 0.3],
    [0.75, 0.98, 0.55, 0.13],
    [1.0, 0.99, 0.95, 0.78],
];

export function heatColor(value: number): RGBA {
    const v = Math.min(1, Math.max(0, value));
    for (let i = 1; i < RAMP.length; ++i) {
        const [p1, r1, g1, b1] = RAMP[i]!;
        if (v <= p1) {
            const [p0, r0, g0, b0] = RAMP[i - 1]!;
            const t = (v - p0) / (p1 - p0);
            return rgba(r0 + t * (r1 - r0), g0 + t * (g1 - g0), b0 + t * (b1 - b0), 1);
        }
    }
    return rgba(1, 1, 1, 1);
}

export class Heatmap {
    rows = 0;
    cols = 0;
    data: Float32Array | null = null;
    peakDb = 0;
    markers: HeatmapMarker[] = [];
    private doa: { dir: Direction; magnitude: number } | null = null;

    setImage(data: Float32Array, rows: number, cols: number, peakDb: number): void {
        this.data = data;
        this.rows = rows;
        this.cols = cols;
        this.peakDb = peakDb;
    }

    setMarkers(markers: HeatmapMarker[]): void {
        this.markers = markers;
    }

    /** Energy-vector overlay from the raw {x, y, z} intensity vector; null
     *  hides it. Magnitude only scales the marker's presence. */
    setEnergyVector(v: Vec3 | null): void {
        if (!v) {
            this.doa = null;
            return;
        }
        const magnitude = Math.hypot(v.x, v.y, v.z);
        this.doa = { dir: toSpherical(v), magnitude };
    }

    /** Screen position of a direction: cell-centre aligned (the grid is
     *  edge-sampled, so sample c sits half a cell right of u = c / cols). */
    directionToXY(d: Direction, frame: HeatmapRect): { x: number; y: number } {
        const halfU = this.cols > 0 ? 0.5 / this.cols : 0;
        const halfV = this.rows > 0 ? 0.5 / this.rows : 0;
        const az = wrapAzimuth(d.azimuth);
        const el = clampElevation(d.elevation);
        const { u, v } = equirectOfDirection({ azimuth: az, elevation: el });
        return { x: frame.x + (u + halfU) * frame.w, y: frame.y + (v + halfV) * frame.h };
    }

    /** Fill a width*height RGBA buffer (e.g. ImageData.data) at the grid's
     *  native resolution (width = cols, height = rows); the host scales. */
    writeRGBA(dst: Uint8ClampedArray): void {
        if (!this.data) {
            return;
        }
        for (let d = 0; d < this.data.length; ++d) {
            const c = heatColor(this.data[d]!);
            dst[d * 4] = Math.round(c.r * 255);
            dst[d * 4 + 1] = Math.round(c.g * 255);
            dst[d * 4 + 2] = Math.round(c.b * 255);
            dst[d * 4 + 3] = 255;
        }
    }

    /** Renderer-only image path: one rect per grid cell. */
    drawCells(r: Renderer, frame: HeatmapRect): void {
        if (!this.data) {
            r.setColor(rgba(0.08, 0.08, 0.1, 1));
            r.rect(frame.x, frame.y, frame.w, frame.h);
            r.fill();
            return;
        }
        const cw = frame.w / this.cols;
        const ch = frame.h / this.rows;
        for (let row = 0; row < this.rows; ++row) {
            for (let col = 0; col < this.cols; ++col) {
                r.setColor(heatColor(this.data[row * this.cols + col]!));
                // +1px bleed avoids hairline seams from fractional cells.
                r.rect(frame.x + col * cw, frame.y + row * ch, cw + 1, ch + 1);
                r.fill();
            }
        }
    }

    drawOverlays(r: Renderer, frame: HeatmapRect): void {
        // Graticule: horizon + front/left/right meridians.
        r.setColor(COLOR_GRID);
        r.setLineWidth(1);
        const horizonY = frame.y + frame.h / 2;
        r.moveTo(frame.x, horizonY);
        r.lineTo(frame.x + frame.w, horizonY);
        r.stroke();
        for (const frac of [0.25, 0.5, 0.75]) {
            r.moveTo(frame.x + frac * frame.w, frame.y);
            r.lineTo(frame.x + frac * frame.w, frame.y + frame.h);
            r.stroke();
        }

        // Cardinal labels along the horizon: B R F L B (azimuth -pi -> +pi,
        // so left of the image is BEHIND-right of the listener).
        r.setColor(COLOR_TEXT);
        r.setFontSize(9);
        const cardinals: Array<[number, string]> = [
            [0, 'B'],
            [0.25, 'R'],
            [0.5, 'F'],
            [0.75, 'L'],
            [1, 'B'],
        ];
        for (const [frac, label] of cardinals) {
            const tx = Math.min(Math.max(frame.x + frac * frame.w - r.textWidth(label) / 2, frame.x + 1),
                frame.x + frame.w - r.textWidth(label) - 1);
            r.fillText(label, tx, horizonY - 3);
        }

        for (const marker of this.markers) {
            const p = this.directionToXY(marker, frame);
            const color = marker.color ?? COLOR_TEXT;
            r.setColor(rgba(color.r, color.g, color.b, 0.9));
            r.setLineWidth(1.5);
            r.arc(p.x, p.y, 5, 0, Math.PI * 2);
            r.stroke();
        }

        if (this.doa) {
            const p = this.directionToXY(this.doa.dir, frame);
            r.setColor(COLOR_DOA);
            r.setLineWidth(1.5);
            r.moveTo(p.x - 7, p.y);
            r.lineTo(p.x + 7, p.y);
            r.stroke();
            r.moveTo(p.x, p.y - 7);
            r.lineTo(p.x, p.y + 7);
            r.stroke();
            r.arc(p.x, p.y, 3.5, 0, Math.PI * 2);
            r.stroke();
        }

        if (this.data) {
            r.setColor(COLOR_TEXT);
            r.setFontSize(10);
            r.fillText(`peak ${this.peakDb.toFixed(1)} dB`, frame.x + 4, frame.y + frame.h - 4);
        }
    }
}
