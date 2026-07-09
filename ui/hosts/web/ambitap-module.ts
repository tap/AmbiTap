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
