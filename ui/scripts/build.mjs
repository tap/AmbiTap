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
await copyFile('hosts/web/index.html', 'dist/web/index.html');

await mkdir('dist/max', { recursive: true });
await build({
    entryPoints: ['hosts/max/ambitap.panner.ts'],
    bundle: true,
    format: 'iife',
    target: 'es2020',
    outfile: 'dist/max/ambitap.panner.js',
});

console.log('built dist/web (open dist/web/index.html) and dist/max (load in [v8ui])');
