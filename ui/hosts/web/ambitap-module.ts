/// Thin typed wrapper over the standalone WASM build of the C ABI
/// (scripts/build-wasm.sh). No DOM or worklet dependencies: usable from the
/// AudioWorklet, the main thread, and node tests alike. The embedder passes
/// a compiled WebAssembly.Module (structured-cloneable into an AudioWorklet).

interface AmbitapExports {
    memory: WebAssembly.Memory;
    _initialize(): void;
    malloc(bytes: number): number;
    free(ptr: number): void;
    ambitap_channel_count(order: number): number;
    ambitap_evaluate_sh(order: number, azimuth: number, elevation: number, out: number): number;
    ambitap_encoder_create(order: number): number;
    ambitap_encoder_destroy(handle: number): void;
    ambitap_encoder_set_direction(handle: number, azimuth: number, elevation: number): number;
    ambitap_encoder_set_gain(handle: number, linear: number): number;
    ambitap_encoder_process(handle: number, mono: number, nFrames: number, out: number): number;
    ambitap_grid_create(order: number, azSteps: number, sampleRate: number, smoothingMs: number): number;
    ambitap_grid_destroy(handle: number): void;
    ambitap_grid_process(handle: number, hoa: number, nFrames: number): number;
    ambitap_grid_snapshot(handle: number, dynamicRangeDb: number, out: number, peakDb: number): number;
    ambitap_vector_create(sampleRate: number, smoothingS: number): number;
    ambitap_vector_destroy(handle: number): void;
    ambitap_vector_process(handle: number, hoa: number, nChannels: number, nFrames: number): number;
    ambitap_vector_value(handle: number, out3: number): number;
    ambitap_rotator_create(order: number): number;
    ambitap_rotator_destroy(handle: number): void;
    ambitap_rotator_set_orientation(handle: number, yaw: number, pitch: number, roll: number): number;
    ambitap_rotator_process(handle: number, input: number, nFrames: number, out: number): number;
    ambitap_layout_preset(name: number, az: number, el: number, cap: number): number;
    ambitap_decoder_matrix(algorithm: number, order: number, nSpeakers: number, az: number, el: number,
        useMaxRe: number, out: number): number;
    ambitap_room_image_sources(dims3: number, source3: number, listener3: number, beta6: number, tMax: number,
        cap: number, t: number, amplitude: number, direction: number, reflections: number): number;
    ambitap_xtc_create(): number;
    ambitap_xtc_destroy(handle: number): void;
    ambitap_xtc_design(handle: number, spanDeg: number, distance: number, regularization: number,
        sampleRate: number): number;
    ambitap_xtc_fir(handle: number, speaker: number, input: number, out: number, cap: number): number;
    ambitap_xtc_info(handle: number, designGainDb: number, makeupLinear: number, latencySamples: number,
        firLength: number): number;
    ambitap_builtin_hrtf_info(order: number, channels: number, length: number, sampleRate: number): number;
    ambitap_builtin_hrtf_hrir(order: number, magls: number, azimuth: number, elevation: number, left: number,
        right: number): number;
}

/** A float array in WASM memory. `view` is re-derived after memory growth
 *  (growth detaches previous ArrayBuffer views). */
export class FloatBuffer {
    constructor(
        private readonly mod: AmbitapModule,
        readonly ptr: number,
        readonly length: number,
    ) {}

    get view(): Float32Array {
        return new Float32Array(this.mod.exports.memory.buffer, this.ptr, this.length);
    }

    set(values: ArrayLike<number>): void {
        this.view.set(values);
    }

    dispose(): void {
        this.mod.exports.free(this.ptr);
    }
}

export class AmbitapModule {
    readonly exports: AmbitapExports;

    constructor(module: WebAssembly.Module) {
        const stub = () => 0;
        const instance = new WebAssembly.Instance(module, {
            env: { emscripten_notify_memory_growth: () => {} },
            wasi_snapshot_preview1: { clock_time_get: stub, fd_close: stub, fd_write: stub, fd_seek: stub },
        });
        this.exports = instance.exports as unknown as AmbitapExports;
        this.exports._initialize();
    }

    allocFloats(length: number): FloatBuffer {
        const ptr = this.exports.malloc(length * 4);
        if (!ptr) {
            throw new Error(`ambitap wasm: malloc(${length * 4}) failed`);
        }
        return new FloatBuffer(this, ptr, length);
    }

    channelCount(order: number): number {
        return this.exports.ambitap_channel_count(order);
    }

    private allocCString(text: string): number {
        // ASCII by construction (preset/algorithm names); TextEncoder is not
        // guaranteed in AudioWorkletGlobalScope.
        const ptr = this.exports.malloc(text.length + 1);
        const view = new Uint8Array(this.exports.memory.buffer, ptr, text.length + 1);
        for (let i = 0; i < text.length; ++i) {
            view[i] = text.charCodeAt(i) & 0x7f;
        }
        view[text.length] = 0;
        return ptr;
    }

    /** Speaker directions of a named library preset (math::layouts). */
    layoutPreset(name: string): Array<{ azimuth: number; elevation: number }> {
        const cap = 64;
        const az = this.allocFloats(cap);
        const el = this.allocFloats(cap);
        const namePtr = this.allocCString(name);
        const count = this.exports.ambitap_layout_preset(namePtr, az.ptr, el.ptr, cap);
        this.exports.free(namePtr);
        if (count < 0) {
            az.dispose();
            el.dispose();
            throw new Error(`ambitap wasm: unknown layout preset '${name}'`);
        }
        const speakers = Array.from({ length: count }, (_, i) => ({
            azimuth: az.view[i]!,
            elevation: el.view[i]!,
        }));
        az.dispose();
        el.dispose();
        return speakers;
    }

    /** Decoder matrix (speakers x channels, row-major) for a speaker set. */
    decoderMatrix(
        algorithm: 'mode_match' | 'allrad' | 'epad',
        order: number,
        speakers: Array<{ azimuth: number; elevation: number }>,
        useMaxRe: boolean,
    ): Float32Array {
        const channels = this.channelCount(order);
        const n = speakers.length;
        const az = this.allocFloats(n);
        const el = this.allocFloats(n);
        az.set(speakers.map((s) => s.azimuth));
        el.set(speakers.map((s) => s.elevation));
        const out = this.allocFloats(n * channels);
        const algPtr = this.allocCString(algorithm);
        const rc = this.exports.ambitap_decoder_matrix(algPtr, order, n, az.ptr, el.ptr, useMaxRe ? 1 : 0, out.ptr);
        this.exports.free(algPtr);
        const result = out.view.slice();
        az.dispose();
        el.dispose();
        out.dispose();
        if (rc !== 0) {
            throw new Error(`ambitap wasm: decoder_matrix(${algorithm}) failed`);
        }
        return result;
    }
}

function check(rc: number, what: string): void {
    if (rc !== 0) {
        throw new Error(`ambitap wasm: ${what} failed`);
    }
}

function create(handle: number, what: string): number {
    if (!handle) {
        throw new Error(`ambitap wasm: ${what} failed`);
    }
    return handle;
}

/** dsp::encoder handle: mono in, channel-major planar HOA out. */
export class Encoder {
    private readonly handle: number;
    readonly channels: number;

    constructor(
        private readonly mod: AmbitapModule,
        order: number,
    ) {
        this.handle = create(mod.exports.ambitap_encoder_create(order), 'encoder_create');
        this.channels = mod.channelCount(order);
    }

    setDirection(azimuth: number, elevation: number): void {
        check(this.mod.exports.ambitap_encoder_set_direction(this.handle, azimuth, elevation), 'encoder_set_direction');
    }

    setGain(linear: number): void {
        check(this.mod.exports.ambitap_encoder_set_gain(this.handle, linear), 'encoder_set_gain');
    }

    process(mono: FloatBuffer, nFrames: number, out: FloatBuffer): void {
        check(this.mod.exports.ambitap_encoder_process(this.handle, mono.ptr, nFrames, out.ptr), 'encoder_process');
    }

    dispose(): void {
        this.mod.exports.ambitap_encoder_destroy(this.handle);
    }
}

/** analysis::soundfield_grid handle. */
export class Grid {
    private readonly handle: number;
    readonly rows: number;
    readonly cols: number;
    private readonly image: FloatBuffer;
    private readonly peak: FloatBuffer;

    constructor(
        private readonly mod: AmbitapModule,
        order: number,
        azSteps: number,
        sampleRate: number,
        smoothingMs: number,
    ) {
        this.handle = create(mod.exports.ambitap_grid_create(order, azSteps, sampleRate, smoothingMs), 'grid_create');
        this.rows = azSteps / 2;
        this.cols = azSteps;
        this.image = mod.allocFloats(this.rows * this.cols);
        this.peak = mod.allocFloats(1);
    }

    process(hoa: FloatBuffer, nFrames: number): void {
        check(this.mod.exports.ambitap_grid_process(this.handle, hoa.ptr, nFrames), 'grid_process');
    }

    /** Smoothed normalized [0,1] heatmap; returns a copy safe to transfer. */
    snapshot(dynamicRangeDb: number): { data: Float32Array; peakDb: number } {
        check(
            this.mod.exports.ambitap_grid_snapshot(this.handle, dynamicRangeDb, this.image.ptr, this.peak.ptr),
            'grid_snapshot',
        );
        return { data: this.image.view.slice(), peakDb: this.peak.view[0]! };
    }

    dispose(): void {
        this.mod.exports.ambitap_grid_destroy(this.handle);
        this.image.dispose();
        this.peak.dispose();
    }
}

/** analysis::energy_vector handle. */
export class EnergyVector {
    private readonly handle: number;
    private readonly out3: FloatBuffer;

    constructor(
        private readonly mod: AmbitapModule,
        sampleRate: number,
        smoothingS: number,
    ) {
        this.handle = create(mod.exports.ambitap_vector_create(sampleRate, smoothingS), 'vector_create');
        this.out3 = mod.allocFloats(3);
    }

    process(hoa: FloatBuffer, nChannels: number, nFrames: number): void {
        check(this.mod.exports.ambitap_vector_process(this.handle, hoa.ptr, nChannels, nFrames), 'vector_process');
    }

    value(): [number, number, number] {
        check(this.mod.exports.ambitap_vector_value(this.handle, this.out3.ptr), 'vector_value');
        const v = this.out3.view;
        return [v[0]!, v[1]!, v[2]!];
    }

    dispose(): void {
        this.mod.exports.ambitap_vector_destroy(this.handle);
        this.out3.dispose();
    }
}

export interface WasmImageSource {
    t: number;
    amplitude: number;
    direction: { x: number; y: number; z: number };
    reflections: number;
}

/** dsp::room::for_each_image via the ABI (the widgets use the TS port in
 *  core/imagesource.ts; this exists so tests can cross-check it). */
export function roomImageSources(
    mod: AmbitapModule,
    dims: [number, number, number],
    source: [number, number, number],
    listener: [number, number, number],
    beta: number[],
    tMax: number,
    cap = 65536,
): WasmImageSource[] {
    const d = mod.allocFloats(3);
    const s = mod.allocFloats(3);
    const l = mod.allocFloats(3);
    const b = mod.allocFloats(6);
    const t = mod.allocFloats(cap);
    const amp = mod.allocFloats(cap);
    const dir = mod.allocFloats(cap * 3);
    const reflPtr = mod.exports.malloc(cap * 4);
    d.set(dims);
    s.set(source);
    l.set(listener);
    b.set(beta);
    const count = mod.exports.ambitap_room_image_sources(d.ptr, s.ptr, l.ptr, b.ptr, tMax, cap, t.ptr, amp.ptr,
        dir.ptr, reflPtr);
    if (count < 0) {
        throw new Error('ambitap wasm: room_image_sources failed');
    }
    const refl = new Int32Array(mod.exports.memory.buffer, reflPtr, cap);
    const out: WasmImageSource[] = [];
    for (let i = 0; i < count; ++i) {
        out.push({
            t: t.view[i]!,
            amplitude: amp.view[i]!,
            direction: { x: dir.view[i * 3]!, y: dir.view[i * 3 + 1]!, z: dir.view[i * 3 + 2]! },
            reflections: refl[i]!,
        });
    }
    mod.exports.free(reflPtr);
    d.dispose();
    s.dispose();
    l.dispose();
    b.dispose();
    t.dispose();
    amp.dispose();
    dir.dispose();
    return out;
}

/** dsp::xtc design handle: redesign + FIR/metadata readback. */
export class XtcDesign {
    private readonly handle: number;

    constructor(private readonly mod: AmbitapModule) {
        this.handle = create(mod.exports.ambitap_xtc_create(), 'xtc_create');
    }

    design(spanDeg: number, distance: number, regularization: number, sampleRate: number): void {
        check(this.mod.exports.ambitap_xtc_design(this.handle, spanDeg, distance, regularization, sampleRate),
            'xtc_design');
    }

    fir(speaker: 0 | 1, input: 0 | 1): Float32Array {
        const buf = this.mod.allocFloats(4096);
        const len = this.mod.exports.ambitap_xtc_fir(this.handle, speaker, input, buf.ptr, buf.length);
        if (len < 0) {
            buf.dispose();
            throw new Error('ambitap wasm: xtc_fir failed');
        }
        const out = buf.view.slice(0, len);
        buf.dispose();
        return out;
    }

    info(): { designGainDb: number; makeupLinear: number; latencySamples: number; firLength: number } {
        const f = this.mod.allocFloats(2);
        const i = this.mod.exports.malloc(8);
        check(this.mod.exports.ambitap_xtc_info(this.handle, f.ptr, f.ptr + 4, i, i + 4), 'xtc_info');
        const ints = new Int32Array(this.mod.exports.memory.buffer, i, 2);
        const out = {
            designGainDb: f.view[0]!,
            makeupLinear: f.view[1]!,
            latencySamples: ints[0]!,
            firLength: ints[1]!,
        };
        f.dispose();
        this.mod.exports.free(i);
        return out;
    }

    dispose(): void {
        this.mod.exports.ambitap_xtc_destroy(this.handle);
    }
}

/** Built-in KEMAR HRIR reconstructed at a direction (both ears, at the
 *  dataset's sample rate) — the XTC designer's plant. */
export function builtinHrtfHrir(
    mod: AmbitapModule,
    order: number,
    magls: boolean,
    azimuth: number,
    elevation: number,
): { left: Float32Array; right: Float32Array } {
    const infoPtr = mod.exports.malloc(12);
    const ratePtr = mod.exports.malloc(4);
    check(mod.exports.ambitap_builtin_hrtf_info(infoPtr, infoPtr + 4, infoPtr + 8, ratePtr), 'builtin_hrtf_info');
    const length = new Int32Array(mod.exports.memory.buffer, infoPtr, 3)[2]!;
    mod.exports.free(infoPtr);
    mod.exports.free(ratePtr);

    const left = mod.allocFloats(length);
    const right = mod.allocFloats(length);
    check(mod.exports.ambitap_builtin_hrtf_hrir(order, magls ? 1 : 0, azimuth, elevation, left.ptr, right.ptr),
        'builtin_hrtf_hrir');
    const out = { left: left.view.slice(), right: right.view.slice() };
    left.dispose();
    right.dispose();
    return out;
}

/** SH rotation handle (embedded profile: compute_sh_rotation +
 *  sh_block_applier, click-free crossfade). out must not alias input. */
export class Rotator {
    private readonly handle: number;

    constructor(
        private readonly mod: AmbitapModule,
        order: number,
    ) {
        this.handle = create(mod.exports.ambitap_rotator_create(order), 'rotator_create');
    }

    setOrientation(yaw: number, pitch: number, roll: number): void {
        check(this.mod.exports.ambitap_rotator_set_orientation(this.handle, yaw, pitch, roll),
            'rotator_set_orientation');
    }

    process(input: FloatBuffer, nFrames: number, out: FloatBuffer): void {
        check(this.mod.exports.ambitap_rotator_process(this.handle, input.ptr, nFrames, out.ptr), 'rotator_process');
    }

    dispose(): void {
        this.mod.exports.ambitap_rotator_destroy(this.handle);
    }
}
