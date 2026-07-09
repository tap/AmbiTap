/// Browser dashboard (UI.md wave 2): the panner steers WASM encoders in an
/// AudioWorklet; grid/vector/meter snapshots come back at ~30 fps and feed
/// the heatmap, DOA, and meter widgets. All four widgets draw through the
/// shared Renderer interface — the heatmap image itself uses the native
/// ImageData blit underneath, per UI.md.

import { degrees, Direction } from '../../core/coords.js';
import { CanvasRenderer } from '../../render/canvas.js';
import { Doa } from '../../widgets/doa.js';
import { Heatmap } from '../../widgets/heatmap.js';
import { SpeakerLayout } from '../../widgets/layout.js';
import { Meters } from '../../widgets/meters.js';
import { Panner, PannerSource } from '../../widgets/panner.js';
import { RotationBall } from '../../widgets/rotation.js';
import { RemoteSurface } from './remote.js';

const ORDER = 3;
const AZ_STEPS = 32;
const DEFAULT_LAYOUT = '7.1.4';

interface Snapshot {
    type: 'snapshot';
    grid: Float32Array;
    rows: number;
    cols: number;
    peakDb: number;
    vec: [number, number, number];
    meters: Float32Array;
    speakers: Float32Array | null;
}

function setupCanvas(id: string): { ctx: CanvasRenderingContext2D; renderer: CanvasRenderer } {
    const canvas = document.getElementById(id) as HTMLCanvasElement;
    const ctx = canvas.getContext('2d')!;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.width;
    const h = canvas.height;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    ctx.scale(dpr, dpr);
    return { ctx, renderer: new CanvasRenderer(ctx, w, h) };
}

const pannerView = setupCanvas('panner');
const heatmapView = setupCanvas('heatmap');
const doaView = setupCanvas('doa');
const rotationView = setupCanvas('rotation');
const layoutView = setupCanvas('layout');
const metersView = setupCanvas('meters');
const readout = document.getElementById('readout')!;
const startButton = document.getElementById('start') as HTMLButtonElement;
const presetSelect = document.getElementById('preset') as HTMLSelectElement;

// Optional remote surface: ?remote=ws://localhost:8090 (scripts/osc-bridge.mjs).
const remoteUrl = new URLSearchParams(window.location.search).get('remote');
const remote = remoteUrl ? new RemoteSurface(remoteUrl, (s) => (document.title = `AmbiTap — ${s}`)) : null;

const SOURCES: Array<Direction & { id: string; frequency: number }> = [
    { id: '1', azimuth: Math.PI / 2, elevation: 0, frequency: 330 },
    { id: '2', azimuth: -Math.PI / 4, elevation: Math.PI / 5, frequency: 660 },
];

let workletPort: MessagePort | null = null;
let dirty = true;

const panner = new Panner({
    onChange: (source: PannerSource) => {
        dirty = true;
        workletPort?.postMessage({
            type: 'direction',
            id: source.id,
            azimuth: source.azimuth,
            elevation: source.elevation,
        });
        remote?.sendDirection(source.id, source.azimuth, source.elevation);
    },
});
for (const s of SOURCES) {
    panner.addSource(s.id, s.azimuth, s.elevation);
}

const heatmap = new Heatmap();
const doa = new Doa();
const meters = new Meters();
const rotation = new RotationBall({
    onChange: (ypr) => {
        dirty = true;
        workletPort?.postMessage({ type: 'orientation', ...ypr });
        remote?.sendOrientation(ypr.yaw, ypr.pitch, ypr.roll);
    },
});
const speakerLayout = new SpeakerLayout();
speakerLayout.setPreset(DEFAULT_LAYOUT);
presetSelect.value = DEFAULT_LAYOUT;
presetSelect.addEventListener('change', () => {
    speakerLayout.setPreset(presetSelect.value);
    workletPort?.postMessage({ type: 'layout', name: presetSelect.value });
    dirty = true;
});

// Offscreen grid-resolution image, scaled onto the heatmap canvas.
const gridCanvas = document.createElement('canvas');
gridCanvas.width = AZ_STEPS;
gridCanvas.height = AZ_STEPS / 2;
const gridCtx = gridCanvas.getContext('2d')!;
const gridImage = gridCtx.createImageData(AZ_STEPS, AZ_STEPS / 2);

function paintHeatmap(): void {
    const { ctx, renderer } = heatmapView;
    const frame = { x: 0, y: 0, w: renderer.width, h: renderer.height };
    if (heatmap.data) {
        heatmap.writeRGBA(gridImage.data);
        gridCtx.putImageData(gridImage, 0, 0);
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(gridCanvas, frame.x, frame.y, frame.w, frame.h);
    } else {
        heatmap.drawCells(renderer, frame); // "no data" well
    }
    heatmap.drawOverlays(renderer, frame);
}

function paintAll(): void {
    panner.draw(pannerView.renderer);
    paintHeatmap();
    doa.draw(doaView.renderer);
    rotation.draw(rotationView.renderer);
    speakerLayout.draw(layoutView.renderer);
    meters.draw(metersView.renderer);
}

function onSnapshot(s: Snapshot): void {
    heatmap.setImage(s.grid, s.rows, s.cols, s.peakDb);
    heatmap.setMarkers(
        SOURCES.map((spec) => {
            const src = panner.getSource(spec.id)!;
            return { id: src.id, azimuth: src.azimuth, elevation: src.elevation, color: src.color };
        }),
    );
    heatmap.setEnergyVector({ x: s.vec[0], y: s.vec[1], z: s.vec[2] });
    doa.update({ x: s.vec[0], y: s.vec[1], z: s.vec[2] });
    meters.update(s.meters);
    if (s.speakers) {
        speakerLayout.setLevels(s.speakers);
    }
    dirty = true;

    const d = doa.direction();
    readout.textContent =
        `energy vector az ${degrees(d.azimuth).toFixed(1)}°  el ${degrees(d.elevation).toFixed(1)}°  ` +
        `strength ${(doa.strength() * 100).toFixed(0)}%   grid peak ${s.peakDb.toFixed(1)} dB`;
}

async function startAudio(): Promise<void> {
    startButton.disabled = true;
    const context = new AudioContext();
    const wasmBytes = await (await fetch('./ambitap.wasm')).arrayBuffer();
    const module = await WebAssembly.compile(wasmBytes);
    await context.audioWorklet.addModule('./worklet.js');

    const node = new AudioWorkletNode(context, 'ambitap-analyzer', {
        numberOfInputs: 0,
        outputChannelCount: [2],
        processorOptions: {
            module,
            order: ORDER,
            azSteps: AZ_STEPS,
            sources: SOURCES,
            layout: presetSelect.value,
        },
    });
    node.port.onmessage = (e) => {
        if ((e.data as Snapshot).type === 'snapshot') {
            onSnapshot(e.data as Snapshot);
        }
    };
    workletPort = node.port;

    const volume = context.createGain();
    volume.gain.value = 0.5;
    node.connect(volume).connect(context.destination);
    await context.resume();
    startButton.textContent = 'running';
}

startButton.addEventListener('click', () => {
    startAudio().catch((err) => {
        startButton.textContent = `failed: ${err}`;
        console.error(err);
    });
});

// Pointer plumbing: the panner, rotation ball, and layout share the shape.
interface PointerWidget {
    pointerDown(x: number, y: number, now: number): boolean;
    pointerMove?(x: number, y: number, now: number): boolean;
    pointerUp?(now: number): boolean;
}

function wirePointer(id: string, widget: PointerWidget): void {
    const canvas = document.getElementById(id) as HTMLCanvasElement;
    const pos = (e: PointerEvent): { x: number; y: number } => {
        const rect = canvas.getBoundingClientRect();
        return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    };
    canvas.addEventListener('pointerdown', (e) => {
        const { x, y } = pos(e);
        if (widget.pointerDown(x, y, e.timeStamp)) {
            canvas.setPointerCapture(e.pointerId);
            dirty = true;
        }
    });
    canvas.addEventListener('pointermove', (e) => {
        const { x, y } = pos(e);
        dirty = (widget.pointerMove?.(x, y, e.timeStamp) ?? false) || dirty;
    });
    canvas.addEventListener('pointerup', (e) => {
        dirty = (widget.pointerUp?.(e.timeStamp) ?? false) || dirty;
    });
}

wirePointer('panner', panner);
wirePointer('rotation', rotation);
wirePointer('layout', { pointerDown: (x, y) => speakerLayout.pointerDown(x, y) });
document.getElementById('rotation')!.addEventListener('dblclick', () => {
    rotation.reset();
    dirty = true;
});

function frame(): void {
    if (dirty) {
        dirty = false;
        paintAll();
    }
    requestAnimationFrame(frame);
}
frame();
