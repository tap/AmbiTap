/// AudioWorklet processor: the verified C++ core (encoder -> soundfield_grid
/// + energy_vector), compiled to WASM, running on the audio thread. Test
/// sources are synthesized here, encoded per-source, summed to one HOA bus,
/// analyzed, and a display snapshot is posted at ~30 fps. The main thread
/// sends `{type:'direction', id, azimuth, elevation}` (from the panner) to
/// steer the encoders — the full loop of UI.md wave 2.
///
/// Bundle with esbuild (scripts/build.mjs) and load via
/// audioWorklet.addModule('worklet.js'); pass the compiled WebAssembly.Module
/// in processorOptions.module.

import { AmbitapModule, Encoder, EnergyVector, FloatBuffer, Grid } from './ambitap-module.js';

const QUANTUM = 128;
const SNAPSHOT_HZ = 30;
const DYNAMIC_RANGE_DB = 40;

interface SourceSpec {
    id: string;
    azimuth: number;
    elevation: number;
    /** Test-tone frequency, Hz. */
    frequency?: number;
}

interface ProcessorOptions {
    module: WebAssembly.Module;
    order?: number;
    azSteps?: number;
    sources?: SourceSpec[];
}

interface Voice {
    id: string;
    encoder: Encoder;
    phase: number;
    frequency: number;
    noise: number; // pink-ish noise state
}

class AmbitapAnalyzer extends AudioWorkletProcessor {
    private readonly mod: AmbitapModule;
    private readonly voices: Voice[] = [];
    private readonly grid: Grid;
    private readonly vector: EnergyVector;
    private readonly channels: number;
    private readonly mono: FloatBuffer;
    private readonly hoaSum: FloatBuffer;
    private readonly hoaTmp: FloatBuffer;
    private readonly snapshotEvery: number;
    private quantum = 0;

    constructor(options?: { processorOptions?: unknown }) {
        super();
        const opts = (options?.processorOptions ?? {}) as ProcessorOptions;
        const order = opts.order ?? 3;
        const azSteps = opts.azSteps ?? 32;

        this.mod = new AmbitapModule(opts.module);
        this.channels = this.mod.channelCount(order);
        this.grid = new Grid(this.mod, order, azSteps, sampleRate, 200);
        this.vector = new EnergyVector(this.mod, sampleRate, 0.01);
        this.mono = this.mod.allocFloats(QUANTUM);
        this.hoaSum = this.mod.allocFloats(this.channels * QUANTUM);
        this.hoaTmp = this.mod.allocFloats(this.channels * QUANTUM);
        this.snapshotEvery = Math.max(1, Math.round(sampleRate / QUANTUM / SNAPSHOT_HZ));

        for (const spec of opts.sources ?? []) {
            const encoder = new Encoder(this.mod, order);
            encoder.setDirection(spec.azimuth, spec.elevation);
            this.voices.push({
                id: spec.id,
                encoder,
                phase: 0,
                frequency: spec.frequency ?? 440,
                noise: 0,
            });
        }

        this.port.onmessage = (e: MessageEvent) => {
            const msg = e.data as { type: string; id?: string; azimuth?: number; elevation?: number };
            if (msg.type === 'direction') {
                const voice = this.voices.find((v) => v.id === msg.id);
                voice?.encoder.setDirection(msg.azimuth ?? 0, msg.elevation ?? 0);
            }
        };
    }

    process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
        const sum = this.hoaSum.view;
        sum.fill(0);

        for (const voice of this.voices) {
            this.synthesize(voice);
            voice.encoder.process(this.mono, QUANTUM, this.hoaTmp);
            const tmp = this.hoaTmp.view;
            for (let i = 0; i < sum.length; ++i) {
                sum[i]! += tmp[i]!;
            }
        }

        this.grid.process(this.hoaSum, QUANTUM);
        this.vector.process(this.hoaSum, this.channels, QUANTUM);

        // Audible monitor: W with a touch of Y for width (not a decoder --
        // wave 3 wires the real binaural path).
        const out = outputs[0];
        if (out && out.length >= 2) {
            const left = out[0]!;
            const right = out[1]!;
            for (let i = 0; i < QUANTUM; ++i) {
                const w = sum[i]! * 0.12;
                const y = sum[QUANTUM + i]! * 0.06;
                left[i] = w + y;
                right[i] = w - y;
            }
        }

        if (++this.quantum % this.snapshotEvery === 0) {
            this.postSnapshot();
        }
        return true;
    }

    private synthesize(voice: Voice): void {
        // Tone + a little noise: enough bandwidth for the intensity vector,
        // a clear line for the ear.
        const mono = this.mono.view;
        const step = (2 * Math.PI * voice.frequency) / sampleRate;
        for (let i = 0; i < QUANTUM; ++i) {
            voice.noise = 0.98 * voice.noise + 0.02 * (Math.random() * 2 - 1);
            mono[i] = 0.6 * Math.sin(voice.phase) + 2.5 * voice.noise;
            voice.phase += step;
        }
        voice.phase %= 2 * Math.PI;
    }

    private postSnapshot(): void {
        const { data, peakDb } = this.grid.snapshot(DYNAMIC_RANGE_DB);
        const vec = this.vector.value();

        // Per-ACN-channel RMS meters, dB.
        const sum = this.hoaSum.view;
        const meters = new Float32Array(this.channels);
        for (let c = 0; c < this.channels; ++c) {
            let acc = 0;
            for (let i = 0; i < QUANTUM; ++i) {
                const s = sum[c * QUANTUM + i]!;
                acc += s * s;
            }
            meters[c] = 10 * Math.log10(acc / QUANTUM + 1e-12);
        }

        this.port.postMessage(
            { type: 'snapshot', grid: data, rows: this.grid.rows, cols: this.grid.cols, peakDb, vec, meters },
            [data.buffer, meters.buffer],
        );
    }
}

registerProcessor('ambitap-analyzer', AmbitapAnalyzer);
