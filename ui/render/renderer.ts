/// The drawing interface every widget targets. Deliberately the intersection
/// of HTML5 canvas 2D and Max's mgraphics (both Cairo-shaped, y-down,
/// radians, arcs sweeping toward +y): a widget written against this runs
/// unmodified in a browser and in v8ui. Keep it minimal — anything only one
/// host can do doesn't belong here.

export interface RGBA {
    r: number; // 0..1 (mgraphics convention; the canvas backend converts)
    g: number;
    b: number;
    a: number;
}

export function rgba(r: number, g: number, b: number, a = 1): RGBA {
    return { r, g, b, a };
}

export interface Renderer {
    readonly width: number;
    readonly height: number;

    save(): void;
    restore(): void;

    beginPath(): void;
    moveTo(x: number, y: number): void;
    lineTo(x: number, y: number): void;
    /** Angles in radians from +x, sweeping toward +y (screen-down). */
    arc(cx: number, cy: number, radius: number, startAngle: number, endAngle: number): void;
    rect(x: number, y: number, w: number, h: number): void;
    closePath(): void;

    setColor(c: RGBA): void;
    setLineWidth(w: number): void;
    fill(): void;
    stroke(): void;

    setFontSize(px: number): void;
    /** Baseline at y (Cairo/mgraphics show_text semantics; canvas
     *  'alphabetic' baseline matches). */
    fillText(text: string, x: number, y: number): void;
    textWidth(text: string): number;
}
