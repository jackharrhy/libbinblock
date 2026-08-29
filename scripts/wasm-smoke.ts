import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { readFile } from 'node:fs/promises';
import { createBinBlockRuntime, type BinBlockWasmFactory } from '../bindings/javascript/binblock.js';
import { decodeReferencePng } from '../apps/browser/reference-assets.js';
import type { ReferenceManifest } from './reference-manifest.js';

const moduleUrl = pathToFileURL(resolve('.build/wasm/binblock.mjs')).href;
const imported = (await import(moduleUrl)) as { default: BinBlockWasmFactory };
const runtime = await createBinBlockRuntime(imported.default);
const diagnostics = runtime.compile(`import "binblock/basic"
size := 8
colors := palette(red: #ff0000, blue: #0000ff)
colors.map(fill).size(size)
`);
if (diagnostics.length !== 0) throw new Error(diagnostics.map((diagnostic) => diagnostic.message).join('\n'));
const output = runtime.outputs()[0];
if (!output || output.cardinality !== 2n) throw new Error('Wasm starter output did not have cardinality 2.');
const image = await runtime.render(0, 0n);
if (image.width !== 8 || image.height !== 8 || image.pixels.length !== 256 || image.pixels[0] !== 255) {
  throw new Error('Wasm starter preview did not match the native raster contract.');
}
const parameter = runtime.parameters()[0];
if (!parameter || parameter.name !== 'size') throw new Error('Wasm starter parameter was not projected.');
runtime.setParameter(parameter.index, parameter.type, 4n);
const resized = await runtime.render(0, 1n);
if (resized.width !== 4 || resized.height !== 4 || resized.pixels[2] !== 255) {
  throw new Error('Wasm parameter recompile did not produce the expected blue preview.');
}

const manifest = JSON.parse(await readFile('reference-set/reference-manifest.json', 'utf8')) as ReferenceManifest;
for (const file of manifest.files)
  runtime.registerAssetMetadata({
    logicalId: file.path,
    contentId: file.encodedSha256,
    width: file.width,
    height: file.height,
    hasEncodedBytes: true,
  });
const referenceSource = await readFile('reference-set/reference-set.binscript', 'utf8');
const referenceDiagnostics = runtime.compile(referenceSource);
if (referenceDiagnostics.length !== 0) throw new Error(referenceDiagnostics.map((diagnostic) => diagnostic.message).join('\n'));
const referenceOutput = runtime.outputs()[0];
if (!referenceOutput || referenceOutput.cardinality !== 4312n)
  throw new Error('Wasm reference program did not remain a lazy 4,312-item output.');
const map18 = runtime.artifacts(0, 34n, 1)[0];
if (!map18) throw new Error('Wasm reference program did not expose map 18.');
const requiredAssets = runtime.requiredAssetContentIds(map18.image);
if (requiredAssets.length !== 1) throw new Error('Map 18 should require exactly one on-demand raster asset.');
const map18Record = manifest.files.find((file) => file.path === 'Gradient Layers Alpha Maps/18.png');
if (!map18Record || requiredAssets[0] !== map18Record.encodedSha256)
  throw new Error('Wasm graph did not retain the map 18 content identity.');
const encodedMap18 = await readFile(`reference-set/${map18Record.path}`);
runtime.hydrateAsset(requiredAssets[0], await decodeReferencePng(encodedMap18), encodedMap18);
const map18Image = await runtime.render(0, 34n);
if (map18Image.width !== 64 || map18Image.height !== 64)
  throw new Error('On-demand Wasm reference asset did not render with its locked dimensions.');
runtime.dispose();
console.log('Wasm smoke test passed.');
