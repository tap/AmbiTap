/// Small iterative radix-2 complex FFT + a magnitude-response helper for
/// the XTC designer's filter plots. Display math only — the audio paths use
/// the library's Ooura FFT.

/** In-place complex FFT; length must be a power of two. */
export function fft(re: Float64Array, im: Float64Array): void {
    const n = re.length;
    if (n !== im.length || (n & (n - 1)) !== 0) {
        throw new Error('fft: length must be a power of two');
    }
    // Bit-reversal permutation.
    for (let i = 1, j = 0; i < n; ++i) {
        let bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            [re[i], re[j]] = [re[j]!, re[i]!];
            [im[i], im[j]] = [im[j]!, im[i]!];
        }
    }
    for (let len = 2; len <= n; len <<= 1) {
        const angle = (-2 * Math.PI) / len;
        const wr = Math.cos(angle);
        const wi = Math.sin(angle);
        for (let i = 0; i < n; i += len) {
            let cr = 1;
            let ci = 0;
            for (let k = 0; k < len / 2; ++k) {
                const a = i + k;
                const b = i + k + len / 2;
                const tr = re[b]! * cr - im[b]! * ci;
                const ti = re[b]! * ci + im[b]! * cr;
                re[b] = re[a]! - tr;
                im[b] = im[a]! - ti;
                re[a] = re[a]! + tr;
                im[a] = im[a]! + ti;
                const ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

/** Complex spectrum (first fftSize/2 + 1 bins) of a real FIR. */
export function spectrum(fir: ArrayLike<number>, fftSize: number): { re: Float64Array; im: Float64Array } {
    const re = new Float64Array(fftSize);
    const im = new Float64Array(fftSize);
    for (let i = 0; i < Math.min(fir.length, fftSize); ++i) {
        re[i] = fir[i]!;
    }
    fft(re, im);
    return { re: re.slice(0, fftSize / 2 + 1), im: im.slice(0, fftSize / 2 + 1) };
}

/** |H(f)| for the first fftSize/2 + 1 bins of a real FIR. */
export function magnitudeResponse(fir: ArrayLike<number>, fftSize: number): Float64Array {
    const { re, im } = spectrum(fir, fftSize);
    const out = new Float64Array(re.length);
    for (let k = 0; k < out.length; ++k) {
        out[k] = Math.hypot(re[k]!, im[k]!);
    }
    return out;
}
