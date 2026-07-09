// Minimal AudioWorklet global-scope declarations (lib.dom.d.ts covers the
// main thread but not the worklet global scope).

declare abstract class AudioWorkletProcessor {
    readonly port: MessagePort;
    constructor(options?: unknown);
    abstract process(inputs: Float32Array[][], outputs: Float32Array[][], parameters: unknown): boolean;
}

declare function registerProcessor(
    name: string,
    ctor: new (options?: { processorOptions?: unknown }) => AudioWorkletProcessor,
): void;

declare const sampleRate: number;
