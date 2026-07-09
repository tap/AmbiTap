/// XTC designer for ambitap.xtc~ (UI.md wave 4): speaker geometry editing
/// plus the design's REAL numbers — the four shipped FIRs' magnitude
/// responses and, when the KEMAR plant is supplied, the performance matrix
/// P = C·H the verification gates measure (ipsilateral flatness /
/// contralateral rejection). No reimplemented design math: H comes from
/// dsp::xtc via the C ABI (ambitap_xtc_fir) or the external's dumpfir, and
/// C from the same built-in LS KEMAR set the design inverted
/// (ambitap_builtin_hrtf_hrir at ±span/2, elevation 0).
///
/// Panes: geometry (drag a speaker: angle sets span 5-120 deg, radius sets
/// distance) + a regularization slider, and the response plot (log-f, the
/// 300 Hz - 6 kHz cancellation band shaded, the +12 dB ceiling ruled).
///
/// Honest-A/B reminder is drawn into the plot: the shipped output sits
/// design_gain_db below bypass by construction — loudness-match before
/// comparing (docs/PERCEPTUAL-VERIFICATION.md bypass rule).

import { degrees } from '../core/coords.js';
import { ValueGate } from '../core/drag.js';
import { spectrum } from '../core/fft.js';
import { Renderer, RGBA, rgba } from '../render/renderer.js';

export interface XtcParams {
    spanDeg: number;
    distance: number;
    regularization: number;
}

export type XtcPhase = 'drag' | 'end';

export interface XtcDesignerOptions {
    onChange?: (params: XtcParams, phase: XtcPhase) => void;
    holdMs?: number;
}

export interface XtcInfo {
    designGainDb: number;
    makeupLinear: number;
    latencySamples: number;
}

interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
}

const FFT_SIZE = 4096; // xtc::k_design_grid
const PLOT_POINTS = 220;
const PLOT_LO_HZ = 40;
const PLOT_HI_HZ = 20000;
const PLOT_TOP_DB = 15;
const PLOT_BOTTOM_DB = -40;
const BAND_LO_HZ = 300; // xtc::k_band_low_hz
const BAND_HI_HZ = 6000; // xtc::k_band_high_hz
const CEILING_DB = 12; // xtc::k_gain_ceiling_db
const MIN_SPAN = 5;
const MAX_SPAN = 120;
const MIN_DISTANCE = 0.1;
const MAX_DISTANCE = 3;

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_PANE = rgba(0.16, 0.17, 0.2);
const COLOR_BAND = rgba(0.35, 0.78, 0.98, 0.08);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.3);
const COLOR_CEILING = rgba(0.98, 0.76, 0.18, 0.6);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const COLOR_HEAD = rgba(0.62, 0.93, 0.53);
const COLOR_SPEAKER = rgba(0.98, 0.76, 0.18);
const COLOR_H_IPSI = rgba(0.35, 0.78, 0.98, 0.9);
const COLOR_H_CROSS = rgba(0.35, 0.78, 0.98, 0.4);
const COLOR_P_IPSI = rgba(0.95, 0.96, 0.98, 0.95);
const COLOR_P_CONTRA = rgba(0.62, 0.93, 0.53, 0.95);

type Firs = [[Float32Array, Float32Array], [Float32Array, Float32Array]];
type Hrirs = { left: Float32Array; right: Float32Array };

interface Curves {
    freqs: Float64Array;
    hIpsiDb: Float64Array;
    hCrossDb: Float64Array;
    pIpsiDb: Float64Array | null;
    pContraDb: Float64Array | null;
}

export class XtcDesigner {
    readonly params: XtcParams = { spanDeg: 20, distance: 1, regularization: 0.5 };
    private info: XtcInfo | null = null;
    private filters: { firs: Firs; sampleRate: number } | null = null;
    /** KEMAR plant HRIR pairs at the two speaker azimuths: [0] = left
     *  speaker (+span/2), [1] = right. Same sample rate as the filters. */
    private plant: [Hrirs, Hrirs] | null = null;
    private curves: Curves | null = null;
    private readonly gate: ValueGate;
    private readonly onChange: (params: XtcParams, phase: XtcPhase) => void;
    private panes: { geo: Rect; slider: Rect; plot: Rect } | null = null;
    private mode: 'none' | 'speaker' | 'reg' = 'none';

    constructor(options: XtcDesignerOptions = {}) {
        this.onChange = options.onChange ?? (() => {});
        this.gate = new ValueGate(options.holdMs ?? 250);
    }

    setParams(params: XtcParams, now: number): boolean {
        if (!this.gate.acceptExternal(now)) {
            return false;
        }
        this.params.spanDeg = Math.min(MAX_SPAN, Math.max(MIN_SPAN, params.spanDeg));
        this.params.distance = Math.min(MAX_DISTANCE, Math.max(MIN_DISTANCE, params.distance));
        this.params.regularization = Math.min(1, Math.max(0, params.regularization));
        return true;
    }

    /** The four shipped FIRs, [speaker][input] with 0 = left. */
    setFilters(firs: Firs, sampleRate: number, info?: XtcInfo): void {
        this.filters = { firs, sampleRate };
        if (info) {
            this.info = info;
        }
        this.curves = null;
    }

    setPlant(leftSpeaker: Hrirs, rightSpeaker: Hrirs): void {
        this.plant = [leftSpeaker, rightSpeaker];
        this.curves = null;
    }

    // --- analysis -----------------------------------------------------------

    /** Log-sampled dB curves; P = C·H only when the plant is present. */
    computeCurves(): Curves | null {
        if (this.curves) {
            return this.curves;
        }
        if (!this.filters) {
            return null;
        }
        const fs = this.filters.sampleRate;
        const bins = FFT_SIZE / 2 + 1;
        const H = this.filters.firs.map((row) => row.map((fir) => spectrum(fir, FFT_SIZE)));
        const C = this.plant
            ? [
                  [spectrum(this.plant[0].left, FFT_SIZE), spectrum(this.plant[1].left, FFT_SIZE)],
                  [spectrum(this.plant[0].right, FFT_SIZE), spectrum(this.plant[1].right, FFT_SIZE)],
              ]
            : null;

        const freqs = new Float64Array(PLOT_POINTS);
        const hIpsiDb = new Float64Array(PLOT_POINTS);
        const hCrossDb = new Float64Array(PLOT_POINTS);
        const pIpsiDb = C ? new Float64Array(PLOT_POINTS) : null;
        const pContraDb = C ? new Float64Array(PLOT_POINTS) : null;
        const db = (m: number): number => 20 * Math.log10(Math.max(m, 1e-6));

        for (let i = 0; i < PLOT_POINTS; ++i) {
            const f = PLOT_LO_HZ * Math.pow(PLOT_HI_HZ / PLOT_LO_HZ, i / (PLOT_POINTS - 1));
            freqs[i] = f;
            const k = Math.min(bins - 1, Math.round((f / (fs / 2)) * (bins - 1)));
            const mag = (s: { re: Float64Array; im: Float64Array }): number => Math.hypot(s.re[k]!, s.im[k]!);
            hIpsiDb[i] = db(mag(H[0]![0]!));
            hCrossDb[i] = db(mag(H[0]![1]!));
            if (C && pIpsiDb && pContraDb) {
                // P[e][0] = sum_s C[e][s] * H[s][0]  (left-input column).
                let p0r = 0, p0i = 0, p1r = 0, p1i = 0;
                for (let s = 0; s < 2; ++s) {
                    const c0 = C[0]![s]!, c1 = C[1]![s]!, h = H[s]![0]!;
                    p0r += c0.re[k]! * h.re[k]! - c0.im[k]! * h.im[k]!;
                    p0i += c0.re[k]! * h.im[k]! + c0.im[k]! * h.re[k]!;
                    p1r += c1.re[k]! * h.re[k]! - c1.im[k]! * h.im[k]!;
                    p1i += c1.re[k]! * h.im[k]! + c1.im[k]! * h.re[k]!;
                }
                pIpsiDb[i] = db(Math.hypot(p0r, p0i));
                pContraDb[i] = db(Math.hypot(p1r, p1i));
            }
        }
        this.curves = { freqs, hIpsiDb, hCrossDb, pIpsiDb, pContraDb };
        return this.curves;
    }

    /** Worst in-band rejection |P_ipsi| - |P_contra| in dB (the X1-style
     *  number), or null without a plant. */
    inBandRejectionDb(): number | null {
        const curves = this.computeCurves();
        if (!curves?.pIpsiDb || !curves.pContraDb) {
            return null;
        }
        let worst = Infinity;
        for (let i = 0; i < curves.freqs.length; ++i) {
            const f = curves.freqs[i]!;
            if (f >= BAND_LO_HZ && f <= BAND_HI_HZ) {
                worst = Math.min(worst, curves.pIpsiDb[i]! - curves.pContraDb[i]!);
            }
        }
        return Number.isFinite(worst) ? worst : null;
    }

    // --- interaction ----------------------------------------------------------

    layout(width: number, height: number): { geo: Rect; slider: Rect; plot: Rect } {
        const pad = 6;
        const geoW = Math.min(width * 0.38, height);
        this.panes = {
            geo: { x: pad, y: pad, w: geoW, h: height - 40 - 3 * pad },
            slider: { x: pad, y: height - 34 - pad, w: geoW, h: 34 },
            plot: { x: 2 * pad + geoW, y: pad, w: width - 3 * pad - geoW, h: height - 2 * pad },
        };
        return this.panes;
    }

    private geoMapping(pane: Rect): { cx: number; cy: number; scale: number } {
        // Head near the bottom, speakers fan out toward the top (front up).
        return {
            cx: pane.x + pane.w / 2,
            cy: pane.y + pane.h * 0.78,
            scale: (pane.h * 0.62) / MAX_DISTANCE,
        };
    }

    private speakerPoint(side: 1 | -1, pane: Rect): { x: number; y: number } {
        const { cx, cy, scale } = this.geoMapping(pane);
        const az = (side * (this.params.spanDeg / 2) * Math.PI) / 180; // + = left
        const r = this.params.distance * scale;
        return { x: cx - r * Math.sin(az), y: cy - r * Math.cos(az) };
    }

    pointerDown(x: number, y: number, _now: number): boolean {
        const panes = this.panes;
        if (!panes) {
            return false;
        }
        for (const side of [1, -1] as const) {
            const p = this.speakerPoint(side, panes.geo);
            if (Math.hypot(p.x - x, p.y - y) < 12) {
                this.mode = 'speaker';
                this.gate.beginDrag();
                return true;
            }
        }
        if (x >= panes.slider.x && x <= panes.slider.x + panes.slider.w && y >= panes.slider.y
            && y <= panes.slider.y + panes.slider.h) {
            this.mode = 'reg';
            this.gate.beginDrag();
            this.applyReg(x, panes.slider);
            return true;
        }
        return false;
    }

    pointerMove(x: number, y: number, _now: number): boolean {
        const panes = this.panes;
        if (!panes || this.mode === 'none') {
            return false;
        }
        if (this.mode === 'speaker') {
            const { cx, cy, scale } = this.geoMapping(panes.geo);
            const az = Math.atan2(cx - x, cy - y); // house top-down inverse
            this.params.spanDeg = Math.min(MAX_SPAN, Math.max(MIN_SPAN, Math.abs(degrees(az)) * 2));
            const r = Math.hypot(x - cx, y - cy) / scale;
            this.params.distance = Math.min(MAX_DISTANCE, Math.max(MIN_DISTANCE, r));
        } else {
            this.applyReg(x, panes.slider);
        }
        this.onChange({ ...this.params }, 'drag');
        return true;
    }

    pointerUp(now: number): boolean {
        if (this.mode === 'none') {
            return false;
        }
        this.mode = 'none';
        this.gate.endDrag(now);
        this.onChange({ ...this.params }, 'end');
        return true;
    }

    isDragging(): boolean {
        return this.mode !== 'none';
    }

    private applyReg(x: number, slider: Rect): void {
        this.params.regularization = Math.min(1, Math.max(0, (x - slider.x - 6) / (slider.w - 12)));
    }

    // --- drawing ----------------------------------------------------------------

    draw(r: Renderer): void {
        const panes = this.layout(r.width, r.height);
        r.setColor(COLOR_BG);
        r.rect(0, 0, r.width, r.height);
        r.fill();
        this.drawGeometry(r, panes.geo);
        this.drawSlider(r, panes.slider);
        this.drawPlot(r, panes.plot);
    }

    private drawGeometry(r: Renderer, pane: Rect): void {
        r.setColor(COLOR_PANE);
        r.rect(pane.x, pane.y, pane.w, pane.h);
        r.fill();
        const { cx, cy, scale } = this.geoMapping(pane);

        // Distance arcs at 1 m steps.
        r.setColor(COLOR_GRID);
        r.setLineWidth(1);
        for (let m = 1; m <= MAX_DISTANCE; ++m) {
            r.arc(cx, cy, m * scale, Math.PI, Math.PI * 2);
            r.stroke();
        }

        // Head + nose (facing front/up).
        r.setColor(COLOR_HEAD);
        r.setLineWidth(1.5);
        r.arc(cx, cy, 9, 0, Math.PI * 2);
        r.stroke();
        r.moveTo(cx, cy - 9);
        r.lineTo(cx - 3.5, cy - 4);
        r.lineTo(cx + 3.5, cy - 4);
        r.closePath();
        r.fill();

        // Speakers + aim lines.
        for (const side of [1, -1] as const) {
            const p = this.speakerPoint(side, pane);
            r.setColor(rgba(COLOR_SPEAKER.r, COLOR_SPEAKER.g, COLOR_SPEAKER.b, 0.35));
            r.setLineWidth(1);
            r.moveTo(cx, cy);
            r.lineTo(p.x, p.y);
            r.stroke();
            r.setColor(COLOR_SPEAKER);
            r.rect(p.x - 5, p.y - 5, 10, 10);
            r.fill();
        }

        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        r.fillText(`span ${this.params.spanDeg.toFixed(1)}°  dist ${this.params.distance.toFixed(2)} m`,
            pane.x + 4, pane.y + 12);
    }

    private drawSlider(r: Renderer, pane: Rect): void {
        r.setColor(COLOR_PANE);
        r.rect(pane.x, pane.y, pane.w, pane.h);
        r.fill();
        const trackY = pane.y + pane.h * 0.62;
        r.setColor(COLOR_GRID);
        r.setLineWidth(2);
        r.moveTo(pane.x + 6, trackY);
        r.lineTo(pane.x + pane.w - 6, trackY);
        r.stroke();
        const hx = pane.x + 6 + this.params.regularization * (pane.w - 12);
        r.setColor(COLOR_H_IPSI);
        r.arc(hx, trackY, 6, 0, Math.PI * 2);
        r.fill();
        r.setColor(COLOR_TEXT);
        r.setFontSize(9);
        r.fillText(`regularization ${this.params.regularization.toFixed(2)}`, pane.x + 4, pane.y + 11);
    }

    private drawPlot(r: Renderer, pane: Rect): void {
        r.setColor(COLOR_PANE);
        r.rect(pane.x, pane.y, pane.w, pane.h);
        r.fill();

        const xOf = (f: number): number =>
            pane.x + (Math.log(f / PLOT_LO_HZ) / Math.log(PLOT_HI_HZ / PLOT_LO_HZ)) * pane.w;
        const yOf = (db: number): number => {
            const clamped = Math.min(PLOT_TOP_DB, Math.max(PLOT_BOTTOM_DB, db));
            return pane.y + ((PLOT_TOP_DB - clamped) / (PLOT_TOP_DB - PLOT_BOTTOM_DB)) * pane.h;
        };

        // Cancellation band, grid, ceiling.
        r.setColor(COLOR_BAND);
        r.rect(xOf(BAND_LO_HZ), pane.y, xOf(BAND_HI_HZ) - xOf(BAND_LO_HZ), pane.h);
        r.fill();
        r.setColor(COLOR_GRID);
        r.setLineWidth(1);
        for (const f of [100, 1000, 10000]) {
            r.moveTo(xOf(f), pane.y);
            r.lineTo(xOf(f), pane.y + pane.h);
            r.stroke();
        }
        for (const db of [0, -20]) {
            r.moveTo(pane.x, yOf(db));
            r.lineTo(pane.x + pane.w, yOf(db));
            r.stroke();
        }
        r.setColor(COLOR_CEILING);
        r.moveTo(pane.x, yOf(CEILING_DB));
        r.lineTo(pane.x + pane.w, yOf(CEILING_DB));
        r.stroke();

        const curves = this.computeCurves();
        if (curves) {
            const polyline = (values: Float64Array, color: RGBA, width: number): void => {
                r.setColor(color);
                r.setLineWidth(width);
                for (let i = 1; i < curves.freqs.length; ++i) {
                    r.moveTo(xOf(curves.freqs[i - 1]!), yOf(values[i - 1]!));
                    r.lineTo(xOf(curves.freqs[i]!), yOf(values[i]!));
                    r.stroke();
                }
            };
            polyline(curves.hCrossDb, COLOR_H_CROSS, 1);
            polyline(curves.hIpsiDb, COLOR_H_IPSI, 1.5);
            if (curves.pIpsiDb && curves.pContraDb) {
                polyline(curves.pContraDb, COLOR_P_CONTRA, 1.5);
                polyline(curves.pIpsiDb, COLOR_P_IPSI, 1.5);
            }
        }

        // Legend + readouts.
        r.setFontSize(9);
        let ty = pane.y + 12;
        const legend = (text: string, color: RGBA): void => {
            r.setColor(color);
            r.fillText(text, pane.x + 6, ty);
            ty += 11;
        };
        legend('|H| ipsi / cross', COLOR_H_IPSI);
        if (curves?.pIpsiDb) {
            legend('P ipsi (flatness)', COLOR_P_IPSI);
            legend('P contra (rejection)', COLOR_P_CONTRA);
            const rej = this.inBandRejectionDb();
            if (rej !== null) {
                legend(`in-band rejection >= ${rej.toFixed(1)} dB`, COLOR_TEXT);
            }
        } else if (curves) {
            legend('(no plant: |H| only)', COLOR_TEXT);
        } else {
            legend('(no design loaded)', COLOR_TEXT);
        }
        if (this.info && this.filters) {
            const ms = (this.info.latencySamples / this.filters.sampleRate) * 1000;
            r.setColor(COLOR_TEXT);
            r.fillText(
                `latency ${ms.toFixed(1)} ms   design ${this.info.designGainDb.toFixed(1)} dB, ` +
                    `makeup ${(20 * Math.log10(Math.max(this.info.makeupLinear, 1e-6))).toFixed(1)} dB — ` +
                    `loudness-match A/B`,
                pane.x + 6,
                pane.y + pane.h - 6,
            );
        }
    }
}
