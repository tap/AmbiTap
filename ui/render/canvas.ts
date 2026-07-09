/// Renderer backend for CanvasRenderingContext2D (browsers).
///
/// The host owns device-pixel-ratio handling: scale the context by DPR and
/// construct/resize this renderer with LOGICAL dimensions.

import { Renderer, RGBA } from './renderer.js';

const FONT_FAMILY = 'system-ui, -apple-system, "Segoe UI", sans-serif';

export class CanvasRenderer implements Renderer {
    private fontSize = 11;

    constructor(
        private readonly ctx: CanvasRenderingContext2D,
        public width: number,
        public height: number,
    ) {
        this.applyFont();
    }

    resize(width: number, height: number): void {
        this.width = width;
        this.height = height;
    }

    save(): void {
        this.ctx.save();
    }
    restore(): void {
        this.ctx.restore();
        this.applyFont();
    }

    beginPath(): void {
        this.ctx.beginPath();
    }
    moveTo(x: number, y: number): void {
        this.ctx.moveTo(x, y);
    }
    lineTo(x: number, y: number): void {
        this.ctx.lineTo(x, y);
    }
    arc(cx: number, cy: number, radius: number, startAngle: number, endAngle: number): void {
        this.ctx.arc(cx, cy, radius, startAngle, endAngle);
    }
    rect(x: number, y: number, w: number, h: number): void {
        this.ctx.rect(x, y, w, h);
    }
    closePath(): void {
        this.ctx.closePath();
    }

    setColor(c: RGBA): void {
        const css = `rgba(${Math.round(c.r * 255)}, ${Math.round(c.g * 255)}, ${Math.round(c.b * 255)}, ${c.a})`;
        this.ctx.fillStyle = css;
        this.ctx.strokeStyle = css;
    }
    setLineWidth(w: number): void {
        this.ctx.lineWidth = w;
    }
    fill(): void {
        this.ctx.fill();
        this.ctx.beginPath();
    }
    stroke(): void {
        this.ctx.stroke();
        this.ctx.beginPath();
    }

    setFontSize(px: number): void {
        this.fontSize = px;
        this.applyFont();
    }
    fillText(text: string, x: number, y: number): void {
        this.ctx.fillText(text, x, y);
    }
    textWidth(text: string): number {
        return this.ctx.measureText(text).width;
    }

    private applyFont(): void {
        this.ctx.font = `${this.fontSize}px ${FONT_FAMILY}`;
        this.ctx.textBaseline = 'alphabetic';
        this.ctx.textAlign = 'left';
    }
}
