/// Room designer for ambitap.room~ (UI.md wave 4): direct-manipulation
/// shoebox geometry + the Allen-Berkley picture drawn from the same math
/// the DSP uses (core/imagesource.ts, cross-checked against
/// room::for_each_image).
///
/// Three panes:
/// - PLAN (top-down, house convention: front/+x up, +y on screen-left) —
///   drag S (source) / L (listener); drag the x=Lx or y=Ly wall to resize;
///   mirrored-room grid + image-source cloud (reflections <= 2) around the
///   room.
/// - SIDE (elevation: +x right, +z up) — drag S / L in x/z; drag the z=Lz
///   ceiling or x=Lx wall.
/// - REFLECTOGRAM — amplitude stems vs time (dB relative to the direct
///   sound), colored by reflection count, with the 30 ms early/tail
///   junction and the broadband RT60 decay slope overlaid, so "does the
///   tail match the knob" is visible while dragging.
///
/// Emits {dims, source, listener} on drag (radians-free — meters); hosts
/// map to room~'s dim_x/../listener_z attributes. External updates are
/// drag-gated (core/drag.ts). Wall reflection coefficients and the
/// broadband RT60 are display parameters (set them from the room~
/// attributes you use); geometry is clamped to the library's ranges.

import { ValueGate } from '../core/drag.js';
import { ImageSource, imageSources, SPEED_OF_SOUND } from '../core/imagesource.js';
import { Renderer, RGBA, rgba } from '../render/renderer.js';

export interface RoomParams {
    dims: [number, number, number];
    source: [number, number, number];
    listener: [number, number, number];
}

export type RoomPhase = 'drag' | 'end';

export interface RoomDesignerOptions {
    onChange?: (params: RoomParams, phase: RoomPhase) => void;
    holdMs?: number;
}

interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
}

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_PANE = rgba(0.16, 0.17, 0.2);
const COLOR_ROOM = rgba(0.2, 0.21, 0.25);
const COLOR_WALL = rgba(0.55, 0.57, 0.62);
const COLOR_MIRROR = rgba(0.45, 0.47, 0.52, 0.25);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const COLOR_SOURCE = rgba(0.98, 0.76, 0.18);
const COLOR_LISTENER = rgba(0.62, 0.93, 0.53);
const COLOR_JUNCTION = rgba(0.75, 0.77, 0.8, 0.5);
const COLOR_RT60 = rgba(0.62, 0.93, 0.53, 0.8);
const STEM_COLORS: RGBA[] = [
    rgba(0.95, 0.96, 0.98), // direct
    rgba(0.98, 0.76, 0.18), // 1st order
    rgba(0.35, 0.78, 0.98), // 2nd
    rgba(0.55, 0.57, 0.62), // 3rd+
];

const GRAB = 10;
const MIN_DIM = 1.0;
const POS_MARGIN = 0.1;
const ER_CUTOFF_S = 0.03; // room::k_er_cutoff_seconds
const REFL_WINDOW_S = 0.1;
const REFL_FLOOR_DB = -60;

/** room~'s defaults (dsp::room member initializers). */
export function defaultRoomParams(): RoomParams {
    return {
        dims: [7.1, 5.3, 3.1],
        source: [3.674, 1.137, 1.977],
        listener: [1.746, 1.711, 0.668],
    };
}

export const DEFAULT_WALL_REFLECTIONS: [number, number, number, number, number, number] = [
    0.9, 0.92, 0.91, 0.93, 0.89, 0.94,
];

type DragMode =
    | 'none'
    | 'src-plan'
    | 'lis-plan'
    | 'src-side'
    | 'lis-side'
    | 'wall-x-plan'
    | 'wall-y-plan'
    | 'wall-x-side'
    | 'wall-z-side';

export class RoomDesigner {
    readonly params: RoomParams = defaultRoomParams();
    beta = [...DEFAULT_WALL_REFLECTIONS] as typeof DEFAULT_WALL_REFLECTIONS;
    rt60 = 0.76; // display slope; room~'s 1 kHz band default
    private readonly gate: ValueGate;
    private readonly onChange: (params: RoomParams, phase: RoomPhase) => void;
    private mode: DragMode = 'none';
    private panes: { plan: Rect; side: Rect; refl: Rect } | null = null;
    private images: ImageSource[] | null = null;
    /** Wall drags freeze the pane mapping (and dims) at grab time — the
     *  scale depends on dims, and re-deriving it mid-drag would fight the
     *  pointer. */
    private anchor: { s: number; ox: number; oy: number; dims: [number, number, number] } | null = null;

    constructor(options: RoomDesignerOptions = {}) {
        this.onChange = options.onChange ?? (() => {});
        this.gate = new ValueGate(options.holdMs ?? 250);
    }

    /** External update (attributes, pattr); drag-gated. */
    setParams(params: RoomParams, now: number): boolean {
        if (!this.gate.acceptExternal(now)) {
            return false;
        }
        this.params.dims = [...params.dims];
        this.params.source = [...params.source];
        this.params.listener = [...params.listener];
        this.clampPositions();
        this.images = null;
        return true;
    }

    setWallReflections(beta: typeof DEFAULT_WALL_REFLECTIONS): void {
        this.beta = [...beta] as typeof DEFAULT_WALL_REFLECTIONS;
        this.images = null;
    }

    setRt60(seconds: number): void {
        this.rt60 = Math.max(0.1, seconds);
    }

    /** The current enumeration (recomputed lazily after edits). */
    imageSet(): ImageSource[] {
        if (!this.images) {
            this.images = imageSources(this.params.dims, this.params.source, this.params.listener, this.beta,
                REFL_WINDOW_S);
            this.images.sort((a, b) => a.t - b.t);
        }
        return this.images;
    }

    // --- geometry ----------------------------------------------------------

    layout(width: number, height: number): { plan: Rect; side: Rect; refl: Rect } {
        const pad = 6;
        const reflH = Math.max(height * 0.3, 70);
        const topH = height - reflH - 3 * pad;
        const planW = (width - 3 * pad) * 0.55;
        this.panes = {
            plan: { x: pad, y: pad, w: planW, h: topH },
            side: { x: 2 * pad + planW, y: pad, w: width - 3 * pad - planW, h: topH },
            refl: { x: pad, y: 2 * pad + topH, w: width - 2 * pad, h: reflH },
        };
        return this.panes;
    }

    /** Plan mapping: front (+x) up, +y on screen-left; room fills ~45% so
     *  the mirror cloud stays visible. */
    private planScale(pane: Rect): { s: number; ox: number; oy: number } {
        const [lx, ly] = this.params.dims;
        const s = Math.min(pane.w / (2.2 * ly!), pane.h / (2.2 * lx!));
        return { s, ox: pane.x + (pane.w - ly! * s) / 2, oy: pane.y + (pane.h - lx! * s) / 2 };
    }

    private planPoint(x: number, y: number, pane: Rect): { x: number; y: number } {
        const { s, ox, oy } = this.planScale(pane);
        return { x: ox + (this.params.dims[1] - y) * s, y: oy + (this.params.dims[0] - x) * s };
    }

    private planInverse(sx: number, sy: number, pane: Rect): { x: number; y: number } {
        const { s, ox, oy } = this.planScale(pane);
        return { x: this.params.dims[0] - (sy - oy) / s, y: this.params.dims[1] - (sx - ox) / s };
    }

    /** Side mapping: +x right, +z up; room fills ~80%. */
    private sideScale(pane: Rect): { s: number; ox: number; oy: number } {
        const [lx, , lz] = this.params.dims;
        const s = Math.min(pane.w / (1.25 * lx!), pane.h / (1.25 * lz!));
        return { s, ox: pane.x + (pane.w - lx! * s) / 2, oy: pane.y + (pane.h - lz! * s) / 2 };
    }

    private sidePoint(x: number, z: number, pane: Rect): { x: number; y: number } {
        const { s, ox, oy } = this.sideScale(pane);
        return { x: ox + x * s, y: oy + (this.params.dims[2] - z) * s };
    }

    private sideInverse(sx: number, sy: number, pane: Rect): { x: number; z: number } {
        const { s, ox, oy } = this.sideScale(pane);
        return { x: (sx - ox) / s, z: this.params.dims[2] - (sy - oy) / s };
    }

    // --- interaction --------------------------------------------------------

    pointerDown(x: number, y: number, _now: number): boolean {
        const panes = this.panes;
        if (!panes) {
            return false;
        }
        const near = (p: { x: number; y: number }): boolean => Math.hypot(p.x - x, p.y - y) < GRAB;

        if (this.inRect(x, y, panes.plan)) {
            const src = this.planPoint(this.params.source[0], this.params.source[1], panes.plan);
            const lis = this.planPoint(this.params.listener[0], this.params.listener[1], panes.plan);
            if (near(src)) {
                return this.begin('src-plan');
            }
            if (near(lis)) {
                return this.begin('lis-plan');
            }
            // Far walls: x = Lx is the room's top edge, y = Ly its left edge.
            const topLeft = this.planPoint(this.params.dims[0], this.params.dims[1], panes.plan);
            const bottomRight = this.planPoint(0, 0, panes.plan);
            if (Math.abs(y - topLeft.y) < GRAB / 2 && x >= topLeft.x - GRAB && x <= bottomRight.x + GRAB) {
                return this.begin('wall-x-plan');
            }
            if (Math.abs(x - topLeft.x) < GRAB / 2 && y >= topLeft.y - GRAB && y <= bottomRight.y + GRAB) {
                return this.begin('wall-y-plan');
            }
            return false;
        }

        if (this.inRect(x, y, panes.side)) {
            const src = this.sidePoint(this.params.source[0], this.params.source[2], panes.side);
            const lis = this.sidePoint(this.params.listener[0], this.params.listener[2], panes.side);
            if (near(src)) {
                return this.begin('src-side');
            }
            if (near(lis)) {
                return this.begin('lis-side');
            }
            const topLeft = this.sidePoint(0, this.params.dims[2], panes.side);
            const bottomRight = this.sidePoint(this.params.dims[0], 0, panes.side);
            if (Math.abs(y - topLeft.y) < GRAB / 2 && x >= topLeft.x - GRAB && x <= bottomRight.x + GRAB) {
                return this.begin('wall-z-side');
            }
            if (Math.abs(x - bottomRight.x) < GRAB / 2 && y >= topLeft.y - GRAB && y <= bottomRight.y + GRAB) {
                return this.begin('wall-x-side');
            }
        }
        return false;
    }

    pointerMove(x: number, y: number, _now: number): boolean {
        const panes = this.panes;
        if (!panes || this.mode === 'none') {
            return false;
        }
        const p = this.params;
        switch (this.mode) {
            case 'src-plan': {
                const m = this.planInverse(x, y, panes.plan);
                p.source[0] = m.x;
                p.source[1] = m.y;
                break;
            }
            case 'lis-plan': {
                const m = this.planInverse(x, y, panes.plan);
                p.listener[0] = m.x;
                p.listener[1] = m.y;
                break;
            }
            case 'src-side': {
                const m = this.sideInverse(x, y, panes.side);
                p.source[0] = m.x;
                p.source[2] = m.z;
                break;
            }
            case 'lis-side': {
                const m = this.sideInverse(x, y, panes.side);
                p.listener[0] = m.x;
                p.listener[2] = m.z;
                break;
            }
            case 'wall-x-plan': {
                const a = this.anchor!;
                const floor = a.oy + a.dims[0] * a.s; // frozen: screen y of x = 0
                p.dims[0] = Math.max(MIN_DIM, (floor - y) / a.s);
                break;
            }
            case 'wall-y-plan': {
                const a = this.anchor!;
                const right = a.ox + a.dims[1] * a.s; // frozen: screen x of y = 0
                p.dims[1] = Math.max(MIN_DIM, (right - x) / a.s);
                break;
            }
            case 'wall-x-side': {
                const a = this.anchor!;
                p.dims[0] = Math.max(MIN_DIM, (x - a.ox) / a.s);
                break;
            }
            case 'wall-z-side': {
                const a = this.anchor!;
                const floor = a.oy + a.dims[2] * a.s; // frozen: screen y of z = 0
                p.dims[2] = Math.max(MIN_DIM, (floor - y) / a.s);
                break;
            }
        }
        this.clampPositions();
        this.images = null;
        this.onChange(this.snapshot(), 'drag');
        return true;
    }

    pointerUp(now: number): boolean {
        if (this.mode === 'none') {
            return false;
        }
        this.mode = 'none';
        this.gate.endDrag(now);
        this.onChange(this.snapshot(), 'end');
        return true;
    }

    isDragging(): boolean {
        return this.mode !== 'none';
    }

    private begin(mode: DragMode): boolean {
        this.mode = mode;
        this.gate.beginDrag();
        const panes = this.panes!;
        const mapping = mode.endsWith('side') ? this.sideScale(panes.side) : this.planScale(panes.plan);
        this.anchor = { ...mapping, dims: [...this.params.dims] };
        return true;
    }

    private clampPositions(): void {
        const p = this.params;
        for (let a = 0; a < 3; ++a) {
            p.dims[a] = Math.max(MIN_DIM, p.dims[a]!);
            const hi = p.dims[a]! - POS_MARGIN;
            p.source[a] = Math.min(hi, Math.max(POS_MARGIN, p.source[a]!));
            p.listener[a] = Math.min(hi, Math.max(POS_MARGIN, p.listener[a]!));
        }
    }

    private snapshot(): RoomParams {
        return {
            dims: [...this.params.dims],
            source: [...this.params.source],
            listener: [...this.params.listener],
        };
    }

    private inRect(x: number, y: number, r: Rect): boolean {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    }

    // --- drawing -------------------------------------------------------------

    draw(r: Renderer): void {
        const panes = this.layout(r.width, r.height);
        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();
        this.drawPlan(r, panes.plan);
        this.drawSide(r, panes.side);
        this.drawReflectogram(r, panes.refl);
    }

    private drawPane(r: Renderer, pane: Rect, label: string): void {
        r.setColor(COLOR_PANE);
        r.rect(pane.x, pane.y, pane.w, pane.h);
        r.fill();
        r.setColor(COLOR_TEXT);
        r.setFontSize(9);
        r.fillText(label, pane.x + 4, pane.y + 11);
    }

    private drawDot(r: Renderer, p: { x: number; y: number }, color: RGBA, label: string): void {
        r.setColor(color);
        r.arc(p.x, p.y, 5, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_TEXT);
        r.setFontSize(9);
        r.fillText(label, p.x + 7, p.y + 3);
    }

    private drawPlan(r: Renderer, pane: Rect): void {
        this.drawPane(r, pane, `plan  ${this.params.dims[0].toFixed(2)} × ${this.params.dims[1].toFixed(2)} m`);
        const [lx, ly] = this.params.dims;

        // Mirrored-room grid (first mirror ring) + image cloud, then the room.
        const { s } = this.planScale(pane);
        r.setColor(COLOR_MIRROR);
        r.setLineWidth(1);
        for (let gx = -1; gx <= 2; ++gx) {
            const p = this.planPoint(gx * lx!, 0, pane);
            if (p.y > pane.y && p.y < pane.y + pane.h) {
                r.moveTo(pane.x, p.y);
                r.lineTo(pane.x + pane.w, p.y);
                r.stroke();
            }
        }
        for (let gy = -1; gy <= 2; ++gy) {
            const p = this.planPoint(0, gy * ly!, pane);
            if (p.x > pane.x && p.x < pane.x + pane.w) {
                r.moveTo(p.x, pane.y);
                r.lineTo(p.x, pane.y + pane.h);
                r.stroke();
            }
        }

        // Image-source cloud (reflections <= 2), projected on the plan.
        const listener = this.params.listener;
        for (const img of this.imageSet()) {
            if (img.reflections === 0 || img.reflections > 2) {
                continue;
            }
            const d = img.t * SPEED_OF_SOUND;
            const ix = listener[0]! + img.direction.x * d;
            const iy = listener[1]! + img.direction.y * d;
            const p = this.planPoint(ix, iy, pane);
            if (!this.inRect(p.x, p.y, pane)) {
                continue;
            }
            const c = STEM_COLORS[Math.min(img.reflections, 3)]!;
            r.setColor(rgba(c.r, c.g, c.b, 0.6));
            r.arc(p.x, p.y, Math.max(1.5, 30 * img.amplitude * s * 0.02), 0, Math.PI * 2);
            r.fill();
        }

        // The room itself.
        const tl = this.planPoint(lx!, ly!, pane);
        const br = this.planPoint(0, 0, pane);
        r.setColor(COLOR_ROOM);
        r.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
        r.fill();
        r.setColor(COLOR_WALL);
        r.setLineWidth(1.5);
        r.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
        r.stroke();
        r.setColor(COLOR_TEXT);
        r.setFontSize(8);
        r.fillText('front', tl.x + (br.x - tl.x) / 2 - r.textWidth('front') / 2, tl.y - 2);

        this.drawDot(r, this.planPoint(this.params.source[0], this.params.source[1], pane), COLOR_SOURCE, 'S');
        this.drawDot(r, this.planPoint(this.params.listener[0], this.params.listener[1], pane), COLOR_LISTENER, 'L');
    }

    private drawSide(r: Renderer, pane: Rect): void {
        this.drawPane(r, pane, `side  ${this.params.dims[0].toFixed(2)} × ${this.params.dims[2].toFixed(2)} m`);
        const [lx, , lz] = this.params.dims;
        const tl = this.sidePoint(0, lz!, pane);
        const br = this.sidePoint(lx!, 0, pane);
        r.setColor(COLOR_ROOM);
        r.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
        r.fill();
        r.setColor(COLOR_WALL);
        r.setLineWidth(1.5);
        r.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
        r.stroke();
        this.drawDot(r, this.sidePoint(this.params.source[0], this.params.source[2], pane), COLOR_SOURCE, 'S');
        this.drawDot(r, this.sidePoint(this.params.listener[0], this.params.listener[2], pane), COLOR_LISTENER, 'L');
    }

    private drawReflectogram(r: Renderer, pane: Rect): void {
        this.drawPane(r, pane, 'reflections  (dB rel. direct)');
        const images = this.imageSet();
        if (images.length === 0) {
            return;
        }
        const direct = images.find((i) => i.reflections === 0);
        const ref = direct?.amplitude ?? images[0]!.amplitude;
        const x0 = pane.x + 4;
        const w = pane.w - 8;
        const yTop = pane.y + 14;
        const h = pane.h - 20;
        const xOf = (t: number): number => x0 + (t / REFL_WINDOW_S) * w;
        const yOf = (db: number): number =>
            yTop + (Math.min(0, Math.max(REFL_FLOOR_DB, db)) / REFL_FLOOR_DB) * h;

        // ER/tail junction, then the RT60 target slope from the direct arrival.
        const junction = xOf(ER_CUTOFF_S + (direct?.t ?? 0));
        r.setColor(COLOR_JUNCTION);
        r.setLineWidth(1);
        r.moveTo(junction, yTop);
        r.lineTo(junction, yTop + h);
        r.stroke();
        if (direct) {
            r.setColor(COLOR_RT60);
            r.moveTo(xOf(direct.t), yOf(0));
            const tEnd = REFL_WINDOW_S;
            r.lineTo(xOf(tEnd), yOf((-60 * (tEnd - direct.t)) / this.rt60));
            r.stroke();
        }

        for (const img of images) {
            const db = 20 * Math.log10(Math.max(img.amplitude / ref, 1e-6));
            if (db < REFL_FLOOR_DB) {
                continue;
            }
            const c = STEM_COLORS[Math.min(img.reflections, 3)]!;
            r.setColor(rgba(c.r, c.g, c.b, img.reflections === 0 ? 1 : 0.8));
            r.setLineWidth(img.reflections === 0 ? 2 : 1);
            const sx = xOf(img.t);
            r.moveTo(sx, yTop + h);
            r.lineTo(sx, yOf(db));
            r.stroke();
        }

        r.setColor(COLOR_TEXT);
        r.setFontSize(8);
        r.fillText('0 ms', x0, pane.y + pane.h - 2);
        r.fillText('100 ms', x0 + w - r.textWidth('100 ms'), pane.y + pane.h - 2);
        r.fillText(`rt60 ${this.rt60.toFixed(2)} s`, junction + 4, yTop + 8);
    }
}
