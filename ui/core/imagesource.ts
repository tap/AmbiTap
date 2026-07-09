/// Shoebox image-source enumeration — a line-for-line port of
/// dsp::room::for_each_image (Allen-Berkley) so the room-designer widget
/// can draw the reflectogram and image cloud in ANY host, including v8ui
/// where no WASM is available. Cross-checked against the C ABI export
/// (ambitap_room_image_sources) by test/wasm.test.ts — change only with
/// the library.
///
/// Conventions: dims/source/listener in meters, walls at 0 and dims[a] per
/// axis; beta = per-wall amplitude reflection coefficients in
/// (x0, x1, y0, y1, z0, z1) order; directions are unit listener -> image
/// (x = front, y = left, z = up).

import { Vec3 } from './coords.js';

export const SPEED_OF_SOUND = 343; // m/s (the library's prototype constant)

export interface ImageSource {
    /** Arrival time, seconds. */
    t: number;
    /** Amplitude: prod(beta^exponent) / distance. */
    amplitude: number;
    /** Unit direction listener -> image. */
    direction: Vec3;
    /** Total reflection count (0 = the direct sound). */
    reflections: number;
}

export function imageSources(
    dims: [number, number, number],
    source: [number, number, number],
    listener: [number, number, number],
    beta: [number, number, number, number, number, number],
    tMax: number,
): ImageSource[] {
    const out: ImageSource[] = [];
    const dMax = tMax * SPEED_OF_SOUND;
    const nRange = [0, 1, 2].map((a) => Math.ceil(dMax / (2 * dims[a]!)) + 1);

    const v = [0, 0, 0];
    for (let px = 0; px <= 1; ++px) {
        for (let py = 0; py <= 1; ++py) {
            for (let pz = 0; pz <= 1; ++pz) {
                const p = [px, py, pz];
                for (let rx = -nRange[0]!; rx <= nRange[0]!; ++rx) {
                    for (let ry = -nRange[1]!; ry <= nRange[1]!; ++ry) {
                        for (let rz = -nRange[2]!; rz <= nRange[2]!; ++rz) {
                            const r = [rx, ry, rz];
                            for (let a = 0; a < 3; ++a) {
                                v[a] = (1 - 2 * p[a]!) * source[a]! + 2 * r[a]! * dims[a]! - listener[a]!;
                            }
                            // sqrt of the squared sum, exactly as the C++
                            // (Math.hypot uses a different algorithm and
                            // drifts in the last ulp).
                            const dist = Math.sqrt(v[0]! * v[0]! + v[1]! * v[1]! + v[2]! * v[2]!);
                            const t = dist / SPEED_OF_SOUND;
                            if (t > tMax || dist < 1e-6) {
                                continue;
                            }
                            let reflections = 0;
                            let amplitude = 1;
                            for (let a = 0; a < 3; ++a) {
                                const e0 = Math.abs(r[a]! - p[a]!);
                                const e1 = Math.abs(r[a]!);
                                reflections += e0 + e1;
                                amplitude *= Math.pow(beta[2 * a]!, e0) * Math.pow(beta[2 * a + 1]!, e1);
                            }
                            amplitude /= dist;
                            out.push({
                                t,
                                amplitude,
                                direction: { x: v[0]! / dist, y: v[1]! / dist, z: v[2]! / dist },
                                reflections,
                            });
                        }
                    }
                }
            }
        }
    }
    return out;
}
