/// Source panner: top-down dome + elevation ring (UI.md widget catalog).
///
/// The disc is an orthographic top-down view — front up, stage-left on
/// screen-left — where dragging sets AZIMUTH (sources render at
/// cos(elevation) of the disc radius, so elevated sources move toward the
/// centre). The half-ring on the right is a side-view ELEVATION control:
/// handle at the top = zenith (+pi/2), right = horizon (0), bottom = nadir.
///
/// Pure model + Renderer calls: no host API, no clocks (hosts pass `now` in
/// milliseconds). Angles are radians throughout, matching the library and
/// the ambitap.*~ attributes. Bidirectional: setDirection() applies external
/// values (OSC, attributes) unless the user is dragging (core/drag.ts).

import {
    clampElevation,
    degrees,
    Direction,
    DiscFrame,
    HALF_PI,
    topDownAzimuth,
    topDownPoint,
    wrapAzimuth,
} from '../core/coords.js';
import { ValueGate } from '../core/drag.js';
import { Renderer, RGBA, rgba } from '../render/renderer.js';

export interface PannerSource extends Direction {
    id: string;
    color?: RGBA;
}

export type PannerPhase = 'drag' | 'end';

export interface PannerOptions {
    /** Fired on every user edit; phase 'end' on release. */
    onChange?: (source: PannerSource, phase: PannerPhase) => void;
    /** Drag-priority window for external updates, ms. */
    holdMs?: number;
}

interface PannerLayout {
    disc: DiscFrame;
    ring: DiscFrame; // ring.radius is the band centreline
}

const SOURCE_COLORS: RGBA[] = [
    rgba(0.98, 0.76, 0.18), // amber
    rgba(0.35, 0.78, 0.98), // sky
    rgba(0.62, 0.93, 0.53), // green
    rgba(0.95, 0.55, 0.75), // pink
];

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_DISC = rgba(0.18, 0.19, 0.22);
const COLOR_RIM = rgba(0.45, 0.47, 0.52);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.35);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const COLOR_RING = rgba(0.45, 0.47, 0.52, 0.5);

const GRAB_RADIUS = 14;
const DOT_RADIUS = 6;
const HANDLE_RADIUS = 7;
const RING_GAP = 18;
const PAD = 8;

export class Panner {
    private readonly sources: PannerSource[] = [];
    private readonly gates = new Map<string, ValueGate>();
    private selectedId: string | null = null;
    private dragMode: 'none' | 'azimuth' | 'elevation' = 'none';
    private layoutCache: PannerLayout | null = null;
    private readonly onChange: (source: PannerSource, phase: PannerPhase) => void;
    private readonly holdMs: number;

    constructor(options: PannerOptions = {}) {
        this.onChange = options.onChange ?? (() => {});
        this.holdMs = options.holdMs ?? 250;
    }

    addSource(id: string, azimuth = 0, elevation = 0, color?: RGBA): PannerSource {
        const source: PannerSource = {
            id,
            azimuth: wrapAzimuth(azimuth),
            elevation: clampElevation(elevation),
            color: color ?? SOURCE_COLORS[this.sources.length % SOURCE_COLORS.length],
        };
        this.sources.push(source);
        this.gates.set(id, new ValueGate(this.holdMs));
        this.selectedId = this.selectedId ?? id;
        return source;
    }

    getSource(id: string): PannerSource | undefined {
        return this.sources.find((s) => s.id === id);
    }

    getSelected(): PannerSource | undefined {
        return this.selectedId ? this.getSource(this.selectedId) : undefined;
    }

    selectSource(id: string): void {
        if (this.getSource(id)) {
            this.selectedId = id;
        }
    }

    /** External (OSC / attribute / automation) update; ignored while the
     *  user is dragging this source or just released it. Returns whether
     *  the value was applied. */
    setDirection(id: string, azimuth: number, elevation: number, now: number): boolean {
        const source = this.getSource(id);
        const gate = this.gates.get(id);
        if (!source || !gate || !gate.acceptExternal(now)) {
            return false;
        }
        source.azimuth = wrapAzimuth(azimuth);
        source.elevation = clampElevation(elevation);
        return true;
    }

    // --- geometry ---------------------------------------------------------

    layout(width: number, height: number): PannerLayout {
        const cx = width / 2;
        const cy = height / 2;
        const outer = Math.min(width, height) / 2 - PAD - HANDLE_RADIUS;
        const ring: DiscFrame = { cx, cy, radius: Math.max(outer, 24) };
        const disc: DiscFrame = { cx, cy, radius: Math.max(ring.radius - RING_GAP, 16) };
        this.layoutCache = { disc, ring };
        return this.layoutCache;
    }

    // --- interaction ------------------------------------------------------

    /** Returns true when the event was consumed (start repainting). */
    pointerDown(x: number, y: number, now: number): boolean {
        const l = this.layoutCache;
        if (!l) {
            return false;
        }
        const dx = x - l.disc.cx;
        const dy = y - l.disc.cy;
        const dist = Math.hypot(dx, dy);

        // Elevation ring: the right-half band around the disc.
        const band = Math.max(GRAB_RADIUS, HANDLE_RADIUS + 5);
        if (dx >= -band && Math.abs(dist - l.ring.radius) <= band && dist > l.disc.radius) {
            const selected = this.getSelected();
            if (selected) {
                this.dragMode = 'elevation';
                this.beginDrag(selected);
                this.applyElevation(selected, x, y, l);
                return true;
            }
            return false;
        }

        if (dist <= l.disc.radius + GRAB_RADIUS / 2) {
            // Nearest source dot within grab range wins; otherwise drag the
            // selected source's azimuth toward the pointer.
            let best: PannerSource | null = null;
            let bestDist = GRAB_RADIUS;
            for (const s of this.sources) {
                const p = topDownPoint(s, l.disc);
                const d = Math.hypot(p.x - x, p.y - y);
                if (d < bestDist) {
                    best = s;
                    bestDist = d;
                }
            }
            const target = best ?? this.getSelected();
            if (target) {
                this.selectedId = target.id;
                this.dragMode = 'azimuth';
                this.beginDrag(target);
                this.applyAzimuth(target, x, y, l);
                return true;
            }
        }
        return false;
    }

    pointerMove(x: number, y: number, now: number): boolean {
        const l = this.layoutCache;
        const selected = this.getSelected();
        if (!l || !selected || this.dragMode === 'none') {
            return false;
        }
        if (this.dragMode === 'azimuth') {
            this.applyAzimuth(selected, x, y, l);
        } else {
            this.applyElevation(selected, x, y, l);
        }
        return true;
    }

    pointerUp(now: number): boolean {
        const selected = this.getSelected();
        if (this.dragMode === 'none' || !selected) {
            return false;
        }
        this.dragMode = 'none';
        this.gates.get(selected.id)?.endDrag(now);
        this.onChange(selected, 'end');
        return true;
    }

    isDragging(): boolean {
        return this.dragMode !== 'none';
    }

    private beginDrag(source: PannerSource): void {
        this.gates.get(source.id)?.beginDrag();
    }

    private applyAzimuth(source: PannerSource, x: number, y: number, l: PannerLayout): void {
        source.azimuth = wrapAzimuth(topDownAzimuth(x, y, l.disc));
        this.onChange(source, 'drag');
    }

    private applyElevation(source: PannerSource, x: number, y: number, l: PannerLayout): void {
        source.elevation = clampElevation(Math.atan2(l.ring.cy - y, x - l.ring.cx));
        this.onChange(source, 'drag');
    }

    // --- drawing ----------------------------------------------------------

    draw(r: Renderer): void {
        const l = this.layout(r.width, r.height);
        const { disc, ring } = l;

        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();

        // Disc + rim.
        r.setColor(COLOR_DISC);
        r.arc(disc.cx, disc.cy, disc.radius, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_RIM);
        r.setLineWidth(1.5);
        r.arc(disc.cx, disc.cy, disc.radius, 0, Math.PI * 2);
        r.stroke();

        // Crosshair + elevation reference circles (30 deg, 60 deg).
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

        // Cardinal labels: front up, left on screen-left (azimuth +pi/2).
        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        const inset = 6;
        r.fillText('F', disc.cx - r.textWidth('F') / 2, disc.cy - disc.radius + inset + 8);
        r.fillText('B', disc.cx - r.textWidth('B') / 2, disc.cy + disc.radius - inset);
        r.fillText('L', disc.cx - disc.radius + inset, disc.cy + 3.5);
        r.fillText('R', disc.cx + disc.radius - inset - r.textWidth('R'), disc.cy + 3.5);

        this.drawElevationRing(r, ring);
        for (const s of this.sources) {
            this.drawSource(r, s, disc);
        }
        this.drawReadout(r);
    }

    private drawElevationRing(r: Renderer, ring: DiscFrame): void {
        // Band: right half, screen angle -pi/2 (top, zenith) .. +pi/2
        // (bottom, nadir).
        r.setColor(COLOR_RING);
        r.setLineWidth(3);
        r.arc(ring.cx, ring.cy, ring.radius, -HALF_PI, HALF_PI);
        r.stroke();

        // Ticks at -90/-45/0/45/90 deg elevation; horizon emphasized.
        for (const el of [-HALF_PI, -HALF_PI / 2, 0, HALF_PI / 2, HALF_PI]) {
            const cos = Math.cos(el);
            const sin = Math.sin(el);
            const len = el === 0 ? 7 : 4;
            r.setColor(el === 0 ? COLOR_TEXT : COLOR_RING);
            r.setLineWidth(el === 0 ? 1.5 : 1);
            r.moveTo(ring.cx + (ring.radius - len) * cos, ring.cy - (ring.radius - len) * sin);
            r.lineTo(ring.cx + (ring.radius + len) * cos, ring.cy - (ring.radius + len) * sin);
            r.stroke();
        }

        const selected = this.getSelected();
        if (!selected) {
            return;
        }
        const color = selected.color ?? COLOR_TEXT;

        // Value arc from the horizon to the current elevation, then the handle.
        r.setColor(rgba(color.r, color.g, color.b, 0.65));
        r.setLineWidth(3);
        if (selected.elevation > 0.001) {
            r.arc(ring.cx, ring.cy, ring.radius, -selected.elevation, 0);
            r.stroke();
        } else if (selected.elevation < -0.001) {
            r.arc(ring.cx, ring.cy, ring.radius, 0, -selected.elevation);
            r.stroke();
        }
        const hx = ring.cx + ring.radius * Math.cos(selected.elevation);
        const hy = ring.cy - ring.radius * Math.sin(selected.elevation);
        r.setColor(color);
        r.arc(hx, hy, HANDLE_RADIUS, 0, Math.PI * 2);
        r.fill();
    }

    private drawSource(r: Renderer, source: PannerSource, disc: DiscFrame): void {
        const color = source.color ?? COLOR_TEXT;
        const p = topDownPoint(source, disc);
        const rim = topDownPoint({ azimuth: source.azimuth, elevation: 0 }, disc);

        // Azimuth ray from the dot to the rim (elevation pulls the dot in).
        r.setColor(rgba(color.r, color.g, color.b, 0.4));
        r.setLineWidth(1);
        r.moveTo(p.x, p.y);
        r.lineTo(rim.x, rim.y);
        r.stroke();

        const selected = source.id === this.selectedId;
        r.setColor(color);
        r.arc(p.x, p.y, DOT_RADIUS, 0, Math.PI * 2);
        r.fill();
        if (selected) {
            r.setLineWidth(1.5);
            r.arc(p.x, p.y, DOT_RADIUS + 3.5, 0, Math.PI * 2);
            r.stroke();
        }
        if (this.sources.length > 1) {
            r.setColor(COLOR_TEXT);
            r.setFontSize(9);
            r.fillText(source.id, p.x + DOT_RADIUS + 3, p.y - DOT_RADIUS);
        }
    }

    private drawReadout(r: Renderer): void {
        const selected = this.getSelected();
        if (!selected) {
            return;
        }
        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        const az = degrees(selected.azimuth).toFixed(1);
        const el = degrees(selected.elevation).toFixed(1);
        r.fillText(`az ${az}°  el ${el}°`, PAD, r.height - PAD);
    }
}
