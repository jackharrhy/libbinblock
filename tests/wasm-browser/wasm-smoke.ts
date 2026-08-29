import createBinBlockModule from './dist-wasm/binblock.mjs';
import { createBinBlockRuntime } from '../../bindings/javascript/binblock.js';
import { BinBlockWebGl2Backend } from '../../bindings/javascript/webgl2-backend.js';

const source = `import "binblock/basic"
size := 64
colors := palette(red: #ff0000, blue: #0000ff)
colors.map(fill).size(size)
`;

const status = document.querySelector<HTMLElement>('#status');
const gallery = document.querySelector<HTMLElement>('#gallery');
if (!status || !gallery) throw new Error('Wasm smoke page is missing required elements.');

const runtime = await createBinBlockRuntime(createBinBlockModule, {
  locateFile: (path) => new URL(`../dist-wasm/${path}`, import.meta.url).href,
});
const diagnostics = runtime.compile(source);
if (diagnostics.length !== 0) throw new Error(diagnostics.map((diagnostic) => diagnostic.message).join('\n'));
const output = runtime.outputs()[0];
if (!output) throw new Error('Starter program has no output.');
const count = Number(output.cardinality < 8n ? output.cardinality : 8n);
const backend = new BinBlockWebGl2Backend();
let gpuValidated = 0;
for (let index = 0; index < count; index += 1) {
  const image = await runtime.render(0, BigInt(index));
  const artifact = runtime.artifacts(0, BigInt(index), 1)[0];
  const gpu = artifact ? backend.render(runtime, artifact.image) : undefined;
  if (gpu) {
    let maxError = 0;
    for (let offset = 0; offset < image.pixels.length; offset += 1)
      maxError = Math.max(maxError, Math.abs(image.pixels[offset] - gpu.pixels[offset]));
    if (maxError > gpu.maxChannelError) throw new Error(`WebGL2 exceeded its ${gpu.maxChannelError}-byte tolerance.`);
    gpuValidated += 1;
  }
  const canvas = document.createElement('canvas');
  canvas.width = image.width;
  canvas.height = image.height;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('2D canvas is unavailable.');
  context.putImageData(new ImageData(Uint8ClampedArray.from(image.pixels), image.width, image.height), 0, 0);
  gallery.append(canvas);
}
const generationBeforeLoss = runtime.generation;
const lost = backend.loseContext();
if (lost && backend.available) throw new Error('WebGL2 backend stayed available after explicit context loss.');
if (lost && backend.render(runtime, runtime.artifacts(0, 0n, 1)[0].image) !== undefined)
  throw new Error('Lost WebGL2 context did not select CPU fallback.');
await runtime.render(0, 0n);
if (runtime.generation !== generationBeforeLoss || runtime.outputs().length !== 1)
  throw new Error('WebGL2 context loss corrupted the C/Wasm program.');
status.textContent = `${output.cardinality} lazy outputs · ${gpuValidated} WebGL2 previews validated · context-loss fallback ${lost ? 'passed' : 'unavailable'} · C/Wasm generation ${runtime.generation}`;
