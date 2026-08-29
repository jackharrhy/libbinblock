import * as esbuild from 'esbuild';
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises';

const result = await esbuild.build({
  entryPoints: ['src/app.ts'],
  bundle: true,
  format: 'esm',
  target: 'es2022',
  outdir: '.build/web-bundle',
  write: false,
  logLevel: 'info',
  external: ['./binblock.mjs'],
});

const script = result.outputFiles?.find((file) => file.path.endsWith('.js'))?.text;
const styles = result.outputFiles?.find((file) => file.path.endsWith('.css'))?.text;
if (!script || !styles) throw new Error('Expected JavaScript and CSS bundles.');

const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="description" content="A live BinScript notebook powered by the portable C/Wasm compiler.">
  <link rel="icon" href="data:,">
  <title>BinScript · Bingen</title>
  <style>${styles}</style>
</head>
<body>
  <main id="app"></main>
  <script type="module" src="./app.js"></script>
</body>
</html>`;

await mkdir('dist', { recursive: true });
await Promise.all([
  writeFile('dist/index.html', html),
  writeFile('dist/app.js', script),
  copyFile('.build/wasm/binblock.mjs', 'dist/binblock.mjs'),
  copyFile('.build/wasm/binblock.wasm', 'dist/binblock.wasm'),
]);

const generatedModule = await readFile('dist/binblock.mjs', 'utf8');
if (generatedModule.includes('virtual:reference-set-archive'))
  throw new Error('Production Wasm build must not embed the reference archive.');
