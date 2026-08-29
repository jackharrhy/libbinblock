import { copyFile, mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { build } from 'esbuild';

const output = resolve('.build/wasm-site');
const wasmOutput = resolve(output, 'dist-wasm');
await mkdir(wasmOutput, { recursive: true });
await Promise.all([
  copyFile(resolve('web/wasm-smoke.html'), resolve(output, 'index.html')),
  copyFile(resolve('.build/wasm/binblock.mjs'), resolve(wasmOutput, 'binblock.mjs')),
  copyFile(resolve('.build/wasm/binblock.wasm'), resolve(wasmOutput, 'binblock.wasm')),
  build({
    entryPoints: [resolve('web/wasm-smoke.ts')],
    outfile: resolve(output, 'wasm-smoke.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: 'es2022',
    external: ['./dist-wasm/binblock.mjs'],
  }),
]);
