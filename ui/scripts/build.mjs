// Build the host bundles:
//   dist/web/  — ES-module bundle + the demo page (open index.html)
//   dist/max/  — single-file classic-script bundle for [v8ui]
import { build } from 'esbuild';
import { copyFile, mkdir } from 'node:fs/promises';

await build({
    entryPoints: ['hosts/web/main.ts'],
    bundle: true,
    format: 'esm',
    target: 'es2020',
    outfile: 'dist/web/main.js',
});
await build({
    entryPoints: ['hosts/web/worklet.ts'],
    bundle: true,
    format: 'iife',
    target: 'es2020',
    outfile: 'dist/web/worklet.js',
});
await copyFile('hosts/web/index.html', 'dist/web/index.html');
// The WASM module is built separately (scripts/build-wasm.sh, needs emsdk);
// stage it next to the page when present.
try {
    await copyFile('dist/wasm/ambitap.wasm', 'dist/web/ambitap.wasm');
} catch {
    console.warn('dist/wasm/ambitap.wasm not found — run scripts/build-wasm.sh for the live demo');
}

await mkdir('dist/max', { recursive: true });
await build({
    entryPoints: [
        'hosts/max/ambitap.panner.ts',
        'hosts/max/ambitap.heatmap.ts',
        'hosts/max/ambitap.doa.ts',
        'hosts/max/ambitap.meters.ts',
        'hosts/max/ambitap.rotation.ts',
        'hosts/max/ambitap.layout.ts',
    ],
    bundle: true,
    format: 'iife',
    target: 'es2020',
    outdir: 'dist/max',
});

console.log('built dist/web (open dist/web/index.html) and dist/max (load in [v8ui])');
