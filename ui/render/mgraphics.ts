/// Renderer backend for Max's mgraphics (v8ui, Max 8.5+).
///
/// mgraphics is Cairo-shaped: paths are implicit (fill/stroke consume the
/// current path), colors are 0..1 rgba, text draws from the current point at
/// the baseline. The v8ui host constructs one of these per paint() with the
/// global mgraphics object; requires mgraphics.relative_coords = 0.

import { Renderer, RGBA } from './renderer.js';

// Typed view of the parts of mgraphics this backend touches.
interface Mgraphics {
    size: number[];
    save(): void;
    restore(): void;
    move_to(x: number, y: number): void;
    line_to(x: number, y: number): void;
    arc(cx: number, cy: number, radius: number, startAngle: number, endAngle: number): void;
    rectangle(x: number, y: number, w: number, h: number): void;
    close_path(): void;
    set_source_rgba(r: number, g: number, b: number, a: number): void;
    set_line_width(w: number): void;
    fill(): void;
    stroke(): void;
    select_font_face(name: string): void;
    set_font_size(px: number): void;
    show_text(text: string): void;
    text_measure(text: string): number[];
}

export class MgraphicsRenderer implements Renderer {
    readonly width: number;
    readonly height: number;
    private hasCurrentPoint = false;

    constructor(private readonly mg: Mgraphics) {
        this.width = mg.size[0] ?? 0;
        this.height = mg.size[1] ?? 0;
        mg.select_font_face('Arial');
    }

    save(): void {
        this.mg.save();
    }
    restore(): void {
        this.mg.restore();
    }

    beginPath(): void {
        // mgraphics has no explicit begin: fill()/stroke() consume the path.
        // Guard against a stray open subpath leaking into the next shape.
        this.hasCurrentPoint = false;
    }
    moveTo(x: number, y: number): void {
        this.mg.move_to(x, y);
        this.hasCurrentPoint = true;
    }
    lineTo(x: number, y: number): void {
        if (!this.hasCurrentPoint) {
            this.mg.move_to(x, y);
            this.hasCurrentPoint = true;
            return;
        }
        this.mg.line_to(x, y);
    }
    arc(cx: number, cy: number, radius: number, startAngle: number, endAngle: number): void {
        this.mg.arc(cx, cy, radius, startAngle, endAngle);
        this.hasCurrentPoint = true;
    }
    rect(x: number, y: number, w: number, h: number): void {
        this.mg.rectangle(x, y, w, h);
        this.hasCurrentPoint = false;
    }
    closePath(): void {
        this.mg.close_path();
    }

    setColor(c: RGBA): void {
        this.mg.set_source_rgba(c.r, c.g, c.b, c.a);
    }
    setLineWidth(w: number): void {
        this.mg.set_line_width(w);
    }
    fill(): void {
        this.mg.fill();
        this.hasCurrentPoint = false;
    }
    stroke(): void {
        this.mg.stroke();
        this.hasCurrentPoint = false;
    }

    setFontSize(px: number): void {
        this.mg.set_font_size(px);
    }
    fillText(text: string, x: number, y: number): void {
        this.mg.move_to(x, y);
        this.mg.show_text(text);
        this.hasCurrentPoint = false;
    }
    textWidth(text: string): number {
        return this.mg.text_measure(text)[0] ?? 0;
    }
}
