/// Rotation / head-orientation ball for ambitap.rotate~ and
/// ambitap.binaural~ (UI.md widget catalog): a top-down arcball with a yaw
/// twist ring.
///
/// - Drag INSIDE the ball: free tumble (Shoemake arcball, view = from
///   above). Drag the OUTER RING: yaw-only twist about world +Z.
/// - The head glyph and the rotated horizon polyline make the library's
///   sign conventions visible at a glance: +yaw turns the nose toward
///   stage-left, +pitch dips the nose below the horizon, +roll drops the
///   right ear.
/// - Emits and accepts intrinsic Z-Y'-X'' yaw/pitch/roll RADIANS — exactly
///   the rotate~/binaural~ attributes and the rotation.h convention.
///   Bidirectional: setYawPitchRoll() displays incoming OSC head-tracking,
///   gated while the user drags (core/drag.ts). reset() (double-click in
///   hosts) restores identity.

import {
    arcballDelta,
    arcballPoint,
    Quat,
    QUAT_IDENTITY,
    quatFromAxisAngle,
    quatFromYawPitchRoll,
    quatMultiply,
    quatNormalize,
    rotateVector,
    YawPitchRoll,
    yawPitchRollFromQuat,
} from '../core/arcball.js';
import { degrees, DiscFrame, Vec3 } from '../core/coords.js';
import { ValueGate } from '../core/drag.js';
import { Renderer, rgba } from '../render/renderer.js';

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_BALL = rgba(0.18, 0.19, 0.22);
const COLOR_RIM = rgba(0.45, 0.47, 0.52);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.35);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const COLOR_HEAD = rgba(0.62, 0.93, 0.53);
const COLOR_HORIZON = rgba(0.35, 0.78, 0.98);
const COLOR_UP = rgba(0.98, 0.76, 0.18);

const PAD = 8;
const RING_GAP = 14;

export type RotationPhase = 'drag' | 'end';

export interface RotationOptions {
    onChange?: (ypr: YawPitchRoll, phase: RotationPhase) => void;
    holdMs?: number;
}

interface RotationLayout {
    ball: DiscFrame;
    ring: DiscFrame;
}

export class RotationBall {
    private q: Quat = { ...QUAT_IDENTITY };
    private readonly gate: ValueGate;
    private readonly onChange: (ypr: YawPitchRoll, phase: RotationPhase) => void;
    private layoutCache: RotationLayout | null = null;
    private mode: 'none' | 'tumble' | 'yaw' = 'none';
    private lastArc: Vec3 | null = null;
    private lastRingAngle = 0;

    constructor(options: RotationOptions = {}) {
        this.onChange = options.onChange ?? (() => {});
        this.gate = new ValueGate(options.holdMs ?? 250);
    }

    yawPitchRoll(): YawPitchRoll {
        return yawPitchRollFromQuat(this.q);
    }

    quaternion(): Quat {
        return { ...this.q };
    }

    /** External update (OSC head-tracking, attribute); gated during drags. */
    setYawPitchRoll(yaw: number, pitch: number, roll: number, now: number): boolean {
        if (!this.gate.acceptExternal(now)) {
            return false;
        }
        this.q = quatFromYawPitchRoll(yaw, pitch, roll);
        return true;
    }

    reset(): void {
        this.q = { ...QUAT_IDENTITY };
        this.onChange(this.yawPitchRoll(), 'end');
    }

    // --- geometry ---------------------------------------------------------

    layout(width: number, height: number): RotationLayout {
        const cx = width / 2;
        const cy = height / 2;
        const outer = Math.max(Math.min(width, height) / 2 - PAD, 24);
        this.layoutCache = {
            ring: { cx, cy, radius: outer },
            ball: { cx, cy, radius: Math.max(outer - RING_GAP, 16) },
        };
        return this.layoutCache;
    }

    // --- interaction ------------------------------------------------------

    pointerDown(x: number, y: number, _now: number): boolean {
        const l = this.layoutCache;
        if (!l) {
            return false;
        }
        const dist = Math.hypot(x - l.ball.cx, y - l.ball.cy);
        if (dist <= l.ball.radius) {
            this.mode = 'tumble';
            this.lastArc = arcballPoint(x, y, l.ball);
            this.gate.beginDrag();
            return true;
        }
        if (dist <= l.ring.radius + 10) {
            this.mode = 'yaw';
            this.lastRingAngle = Math.atan2(y - l.ring.cy, x - l.ring.cx);
            this.gate.beginDrag();
            return true;
        }
        return false;
    }

    pointerMove(x: number, y: number, _now: number): boolean {
        const l = this.layoutCache;
        if (!l || this.mode === 'none') {
            return false;
        }
        if (this.mode === 'tumble' && this.lastArc) {
            const arc = arcballPoint(x, y, l.ball);
            const dv = arcballDelta(this.lastArc, arc); // view space: x right, y up, z out
            // Top-down view basis in world axes: view x = -y_world (screen
            // right is stage-right), view y = +x_world (screen up is front),
            // view z = +z_world (viewer looks down). Remap the delta's axis.
            const dw = quatNormalize({ w: dv.w, x: dv.y, y: -dv.x, z: dv.z });
            this.q = quatNormalize(quatMultiply(dw, this.q));
            this.lastArc = arc;
        } else if (this.mode === 'yaw') {
            const angle = Math.atan2(y - l.ring.cy, x - l.ring.cx);
            let delta = angle - this.lastRingAngle;
            if (delta > Math.PI) {
                delta -= 2 * Math.PI;
            } else if (delta < -Math.PI) {
                delta += 2 * Math.PI;
            }
            // Screen angle grows clockwise (y down); +yaw is counterclockwise
            // seen from above.
            this.q = quatNormalize(quatMultiply(quatFromAxisAngle(0, 0, 1, -delta), this.q));
            this.lastRingAngle = angle;
        }
        this.onChange(this.yawPitchRoll(), 'drag');
        return true;
    }

    pointerUp(now: number): boolean {
        if (this.mode === 'none') {
            return false;
        }
        this.mode = 'none';
        this.lastArc = null;
        this.gate.endDrag(now);
        this.onChange(this.yawPitchRoll(), 'end');
        return true;
    }

    isDragging(): boolean {
        return this.mode !== 'none';
    }

    // --- drawing ----------------------------------------------------------

    private project(v: Vec3, f: DiscFrame): { x: number; y: number } {
        // Orthographic from above: front up, stage-left on screen-left.
        return { x: f.cx - f.radius * v.y, y: f.cy - f.radius * v.x };
    }

    draw(r: Renderer): void {
        const { ball, ring } = this.layout(r.width, r.height);

        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();

        // Yaw ring, then the ball.
        r.setColor(COLOR_GRID);
        r.setLineWidth(3);
        r.arc(ring.cx, ring.cy, ring.radius, 0, Math.PI * 2);
        r.stroke();
        r.setColor(COLOR_BALL);
        r.arc(ball.cx, ball.cy, ball.radius, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_RIM);
        r.setLineWidth(1.5);
        r.arc(ball.cx, ball.cy, ball.radius, 0, Math.PI * 2);
        r.stroke();

        this.drawHorizon(r, ball);
        this.drawHead(r, ball);
        this.drawAxes(r, ball);

        // Readout.
        const ypr = this.yawPitchRoll();
        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        r.fillText(
            `y ${degrees(ypr.yaw).toFixed(1)}°  p ${degrees(ypr.pitch).toFixed(1)}°  r ${degrees(ypr.roll).toFixed(1)}°`,
            PAD,
            r.height - PAD,
        );
    }

    /** The rotated horizon great circle, brighter where it rises above the
     *  listener plane — pitch and roll become immediately visible. */
    private drawHorizon(r: Renderer, ball: DiscFrame): void {
        const SEGMENTS = 48;
        r.setLineWidth(1.5);
        let prev: { p: { x: number; y: number }; z: number } | null = null;
        for (let s = 0; s <= SEGMENTS; ++s) {
            const theta = (s / SEGMENTS) * 2 * Math.PI;
            const v = rotateVector(this.q, { x: Math.cos(theta), y: Math.sin(theta), z: 0 });
            const p = this.project(v, ball);
            if (prev) {
                const above = (prev.z + v.z) / 2 >= 0;
                r.setColor(rgba(COLOR_HORIZON.r, COLOR_HORIZON.g, COLOR_HORIZON.b, above ? 0.85 : 0.25));
                r.moveTo(prev.p.x, prev.p.y);
                r.lineTo(p.x, p.y);
                r.stroke();
            }
            prev = { p, z: v.z };
        }
    }

    /** Head glyph: circle + nose triangle + ear ticks, yaw-rotated. */
    private drawHead(r: Renderer, ball: DiscFrame): void {
        const headR = ball.radius * 0.3;
        const front = rotateVector(this.q, { x: 1, y: 0, z: 0 });
        const noseAz = Math.atan2(front.y, front.x); // yaw of the projected nose

        r.setColor(COLOR_HEAD);
        r.setLineWidth(1.5);
        r.arc(ball.cx, ball.cy, headR, 0, Math.PI * 2);
        r.stroke();

        // Nose triangle at the head edge, pointing along the yaw direction.
        const nose = (az: number, radius: number): { x: number; y: number } => ({
            x: ball.cx - radius * Math.sin(az),
            y: ball.cy - radius * Math.cos(az),
        });
        const tip = nose(noseAz, headR + 8);
        const baseL = nose(noseAz + 0.35, headR - 1);
        const baseR = nose(noseAz - 0.35, headR - 1);
        r.moveTo(tip.x, tip.y);
        r.lineTo(baseL.x, baseL.y);
        r.lineTo(baseR.x, baseR.y);
        r.closePath();
        r.fill();

        // Ear ticks perpendicular to the nose.
        for (const side of [1, -1]) {
            const e0 = nose(noseAz + (side * Math.PI) / 2, headR - 2);
            const e1 = nose(noseAz + (side * Math.PI) / 2, headR + 4);
            r.moveTo(e0.x, e0.y);
            r.lineTo(e1.x, e1.y);
            r.stroke();
        }
    }

    /** Rotated front (F) and up (U) markers; U sits centred at identity and
     *  slides outward as the field tilts. */
    private drawAxes(r: Renderer, ball: DiscFrame): void {
        const front = rotateVector(this.q, { x: 1, y: 0, z: 0 });
        const up = rotateVector(this.q, { x: 0, y: 0, z: 1 });
        const pf = this.project(front, ball);
        const pu = this.project(up, ball);

        r.setFontSize(9);
        r.setColor(COLOR_HEAD);
        r.arc(pf.x, pf.y, 3, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_TEXT);
        r.fillText('F', pf.x + 5, pf.y + 3);

        r.setColor(COLOR_UP);
        r.arc(pu.x, pu.y, 3, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_TEXT);
        r.fillText('U', pu.x + 5, pu.y + 3);
    }
}
