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

import { AmbitapModule, Encoder, EnergyVector, FloatBuffer, Grid, Rotator } from './ambitap-module.js';

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
    /** Initial decoder layout preset for per-speaker level metering. */
    layout?: string;
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
    private readonly rotator: Rotator;
    private readonly order: number;
    private readonly channels: number;
    private readonly mono: FloatBuffer;
    private readonly hoaSum: FloatBuffer;
    private readonly hoaTmp: FloatBuffer;
    private readonly hoaRot: FloatBuffer;
    private readonly snapshotEvery: number;
    private quantum = 0;
    private decoder: { matrix: Float32Array; speakers: number } | null = null;

    constructor(options?: { processorOptions?: unknown }) {
        super();
        const opts = (options?.processorOptions ?? {}) as ProcessorOptions;
        const order = opts.order ?? 3;
        const azSteps = opts.azSteps ?? 32;

        this.mod = new AmbitapModule(opts.module);
        this.order = order;
        this.channels = this.mod.channelCount(order);
        this.grid = new Grid(this.mod, order, azSteps, sampleRate, 200);
        this.vector = new EnergyVector(this.mod, sampleRate, 0.01);
        this.rotator = new Rotator(this.mod, order);
        this.mono = this.mod.allocFloats(QUANTUM);
        this.hoaSum = this.mod.allocFloats(this.channels * QUANTUM);
        this.hoaTmp = this.mod.allocFloats(this.channels * QUANTUM);
        this.hoaRot = this.mod.allocFloats(this.channels * QUANTUM);
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
        if (opts.layout) {
            this.setLayout(opts.layout);
        }

        this.port.onmessage = (e: MessageEvent) => {
            const msg = e.data as {
                type: string;
                id?: string;
                azimuth?: number;
                elevation?: number;
                yaw?: number;
                pitch?: number;
                roll?: number;
                name?: string;
            };
            if (msg.type === 'direction') {
                const voice = this.voices.find((v) => v.id === msg.id);
                voice?.encoder.setDirection(msg.azimuth ?? 0, msg.elevation ?? 0);
            } else if (msg.type === 'orientation') {
                this.rotator.setOrientation(msg.yaw ?? 0, msg.pitch ?? 0, msg.roll ?? 0);
            } else if (msg.type === 'layout' && msg.name) {
                this.setLayout(msg.name);
            }
        };
    }

    /** Rebuild the metering decode matrix (control path, allocation is fine). */
    private setLayout(name: string): void {
        try {
            const speakers = this.mod.layoutPreset(name);
            const matrix = this.mod.decoderMatrix('allrad', this.order, speakers, true);
            this.decoder = { matrix, speakers: speakers.length };
        } catch (err) {
            this.decoder = null;
            this.port.postMessage({ type: 'error', message: String(err) });
        }
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

        // Field rotation (click-free crossfade in the applier), then analyze
        // the ROTATED bus — the heatmap counter-rotates under the ball.
        this.rotator.process(this.hoaSum, QUANTUM, this.hoaRot);
        this.grid.process(this.hoaRot, QUANTUM);
        this.vector.process(this.hoaRot, this.channels, QUANTUM);

        // Audible monitor: W with a touch of Y for width (a real binaural
        // path is a later wave).
        const rot = this.hoaRot.view;
        const out = outputs[0];
        if (out && out.length >= 2) {
            const left = out[0]!;
            const right = out[1]!;
            for (let i = 0; i < QUANTUM; ++i) {
                const w = rot[i]! * 0.12;
                const y = rot[QUANTUM + i]! * 0.06;
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

        // Per-ACN-channel RMS meters (dB) on the rotated bus.
        const rot = this.hoaRot.view;
        const meters = new Float32Array(this.channels);
        for (let c = 0; c < this.channels; ++c) {
            let acc = 0;
            for (let i = 0; i < QUANTUM; ++i) {
                const s = rot[c * QUANTUM + i]!;
                acc += s * s;
            }
            meters[c] = 10 * Math.log10(acc / QUANTUM + 1e-12);
        }

        // Per-speaker RMS (dB) through the metering decode matrix.
        let speakers: Float32Array | null = null;
        if (this.decoder) {
            const { matrix, speakers: n } = this.decoder;
            speakers = new Float32Array(n);
            for (let s = 0; s < n; ++s) {
                let acc = 0;
                for (let i = 0; i < QUANTUM; ++i) {
                    let feed = 0;
                    for (let c = 0; c < this.channels; ++c) {
                        feed += matrix[s * this.channels + c]! * rot[c * QUANTUM + i]!;
                    }
                    acc += feed * feed;
                }
                speakers[s] = 10 * Math.log10(acc / QUANTUM + 1e-12);
            }
        }

        const transfers: ArrayBuffer[] = [data.buffer as ArrayBuffer, meters.buffer as ArrayBuffer];
        if (speakers) {
            transfers.push(speakers.buffer as ArrayBuffer);
        }
        this.port.postMessage(
            { type: 'snapshot', grid: data, rows: this.grid.rows, cols: this.grid.cols, peakDb, vec, meters, speakers },
            transfers,
        );
    }
}

registerProcessor('ambitap-analyzer', AmbitapAnalyzer);
