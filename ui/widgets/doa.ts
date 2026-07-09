/// Energy-vector DOA dot: the smoothed active-intensity vector from
/// analysis::energy_vector (or ambitap.energyvec~) on the same top-down
/// projection as the panner — front up, stage-left on screen-left, the dot
/// pulled toward the centre by elevation.
///
/// Vector MAGNITUDE maps to the dot's size and opacity, normalized by a
/// slow peak follower, so the display reads localization QUALITY (a diffuse
/// field collapses the vector) rather than just direction — the ||rE||
/// story from the notebooks.

import { Direction, DiscFrame, topDownPoint, toSpherical, Vec3 } from '../core/coords.js';
import { Renderer, RGBA, rgba } from '../render/renderer.js';

const COLOR_BG = rgba(0.13, 0.14, 0.16);
const COLOR_DISC = rgba(0.18, 0.19, 0.22);
const COLOR_RIM = rgba(0.45, 0.47, 0.52);
const COLOR_GRID = rgba(0.45, 0.47, 0.52, 0.35);
const COLOR_TEXT = rgba(0.75, 0.77, 0.8);
const PAD = 8;

export class Doa {
    private dir: Direction = { azimuth: 0, elevation: 0 };
    private magnitude = 0;
    private peak = 1e-6;
    color: RGBA = rgba(0.35, 0.78, 0.98);

    /** Feed the raw intensity vector {x front, y left, z up}. */
    update(v: Vec3): void {
        this.magnitude = Math.hypot(v.x, v.y, v.z);
        this.peak = Math.max(this.peak * 0.995, this.magnitude, 1e-6);
        if (this.magnitude > 1e-9) {
            this.dir = toSpherical(v);
        }
    }

    /** Magnitude relative to the recent peak, 0..1. */
    strength(): number {
        return Math.min(1, this.magnitude / this.peak);
    }

    direction(): Direction {
        return this.dir;
    }

    draw(r: Renderer): void {
        const disc: DiscFrame = {
            cx: r.width / 2,
            cy: r.height / 2,
            radius: Math.max(Math.min(r.width, r.height) / 2 - PAD, 16),
        };

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

        r.setColor(COLOR_TEXT);
        r.setFontSize(10);
        r.fillText('F', disc.cx - r.textWidth('F') / 2, disc.cy - disc.radius + 12);

        const s = this.strength();
        const p = topDownPoint(this.dir, disc);
        // Direction ray, then the dot: size + alpha carry ||v|| / recent peak.
        r.setColor(rgba(this.color.r, this.color.g, this.color.b, 0.25 + 0.4 * s));
        r.setLineWidth(1);
        r.moveTo(disc.cx, disc.cy);
        r.lineTo(p.x, p.y);
        r.stroke();
        r.setColor(rgba(this.color.r, this.color.g, this.color.b, 0.3 + 0.7 * s));
        r.arc(p.x, p.y, 3 + 5 * s, 0, Math.PI * 2);
        r.fill();
    }
}
