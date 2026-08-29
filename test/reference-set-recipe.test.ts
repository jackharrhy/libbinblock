import assert from 'node:assert/strict';
import test from 'node:test';
import { createCanvas, loadImage } from 'canvas';
import { readdir } from 'node:fs/promises';
import { join } from 'node:path';
import { REFERENCE_SET_IMAGE_OPERATIONS } from '../legacy-ts/src/reference-set-operations.js';
import {
  createDownscaledRecipe,
  createCustomPermutationRecipe,
  createFlatColorRecipe,
  createForegroundAlphaRecipe,
  createForegroundCompositeRecipe,
  createGradientMaskRecipe,
  createReferenceSetRasterRecipe,
  createOrderedResultsRecipe,
} from '../legacy-ts/src/reference-set-recipe.js';
import { compileRecipe, type ImageData } from '../legacy-ts/src/recipe-executor.js';
import { renderRedPrintArchiveOutput } from '../legacy-ts/src/legacy.js';
import { parseRecipeDocument } from '../legacy-ts/src/recipe-schema.js';
import { renderBlock } from '../legacy-ts/src/core.js';
import type { RasterAsset } from '../legacy-ts/src/recipe-schema.js';
import type { RasterData } from '../legacy-ts/src/legacy.js';

interface ArchiveFixture {
  files: Array<{ path: string; sha256?: string }>;
}

type RasterSources = Record<number, RasterData>;

async function imagePixels(path: string): Promise<ImageData> {
  const image = await loadImage(path);
  const canvas = createCanvas(image.width, image.height);
  canvas.getContext('2d').drawImage(image, 0, 0);
  return { width: image.width, height: image.height, pixels: canvas.getContext('2d').getImageData(0, 0, image.width, image.height).data };
}

test('complete reference set maps source folders into composable semantic stages', async () => {
  const archive: ArchiveFixture = {
    files: [{ path: 'col/col_blue_hi.png' }, { path: 'blue 64-8 24 bit/col_blue_hi.png' }, { path: 'result/ColBinSet_0000.png' }],
  };
  const recipe = createReferenceSetRasterRecipe(archive);
  const pixels = new Uint8ClampedArray([0, 0, 255, 255]);
  const result = await compileRecipe(recipe, {
    resolveRaster: async () => ({ width: 1, height: 1, pixels }),
  });

  assert.equal(recipe.metadata?.imageCount, 3);
  assert.deepEqual(
    result.outputs.map((output) => output.path),
    ['flat-color/col_blue_hi.png', 'downscaled/col_blue_hi.png', 'ordered-results/ColBinSet_0000.png'],
  );
  assert.deepEqual(
    result.outputs.map((output) => output.properties.reproduction),
    ['raster-exact', 'raster-exact', 'raster-exact'],
  );
});

test('complete reference set rejects archive paths without a family mapping', () => {
  assert.throws(() => createReferenceSetRasterRecipe({ files: [{ path: 'unknown/file.png' }] }), /No reference-set recipe family maps/);
});

test('flat-color recipe expands arbitrary palette values into analytic artifacts', async () => {
  const result = await compileRecipe(
    createFlatColorRecipe([
      ['ink', '#123456'],
      ['paper', '#fedcba'],
    ]),
  );
  assert.deepEqual(
    result.outputs.map((output) => output.path),
    ['flat-color/ink.png', 'flat-color/paper.png'],
  );
  assert.deepEqual([...result.outputs[0].image.pixels.subarray(0, 4)], [18, 52, 86, 255]);
  assert.equal(result.outputs[0].properties.reproduction, 'analytic');
});

test('downscaled recipe reproduces all 88 default fixtures pixel-exactly', async () => {
  const directory = 'reference-set/blue 64-8 24 bit';
  const filenames = (await readdir(directory)).filter((filename) => filename.endsWith('.png')).sort();
  const archive: ArchiveFixture = { files: filenames.map((filename) => ({ path: `blue 64-8 24 bit/${filename}` })) };
  const result = await compileRecipe(createDownscaledRecipe(archive), {
    resolveRaster: (_assetId: string, asset: RasterAsset) => imagePixels(join('reference-set', asset.path)),
  });
  assert.equal(result.outputs.length, 88);
  for (const output of result.outputs) {
    const expected = await imagePixels(join('reference-set/blue 64-8 24 bit', output.key));
    assert.deepEqual([...output.image.pixels], [...expected.pixels], output.key);
  }
});

test('gradient-mask recipe reproduces all 19 mask dimensions and alpha channels exactly', async () => {
  const directory = 'reference-set/Gradient Layers Alpha Maps';
  const filenames = (await readdir(directory)).filter((filename) => filename.endsWith('.png')).sort();
  const archive: ArchiveFixture = { files: filenames.map((filename) => ({ path: `Gradient Layers Alpha Maps/${filename}` })) };
  const result = await compileRecipe(createGradientMaskRecipe(archive), {
    operations: REFERENCE_SET_IMAGE_OPERATIONS,
    resolveRaster: (_assetId: string, asset: RasterAsset) => imagePixels(join('reference-set', asset.path)),
  });
  assert.equal(result.outputs.length, 19);
  for (const output of result.outputs) {
    const expected = await imagePixels(join(directory, `${output.key}.png`));
    assert.equal(output.image.width, expected.width, output.key);
    assert.equal(output.image.height, expected.height, output.key);
    assert.deepEqual(
      [...output.image.pixels].filter((_value, index) => index % 4 === 3),
      [...expected.pixels].filter((_value, index) => index % 4 === 3),
      output.key,
    );
    if (output.key === '18') assert.deepEqual([...output.image.pixels], [...expected.pixels], output.key);
  }
});

test('foreground-alpha recipe feeds a pinned field stage into a configurable tint stage', async () => {
  const directory = 'reference-set/red FG-Alpha';
  const filenames = (await readdir(directory))
    .filter((filename) => filename.endsWith('.png'))
    .sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
  const archive: ArchiveFixture = { files: filenames.map((filename) => ({ path: `red FG-Alpha/${filename}` })) };
  const result = await compileRecipe(createForegroundAlphaRecipe(archive), {
    resolveRaster: (_assetId: string, asset: RasterAsset) => imagePixels(join('reference-set', asset.path)),
  });
  assert.equal(result.stages.get('foreground-alpha-fields')?.length, 22);
  assert.equal(result.outputs.length, 22);
  for (const output of result.outputs) {
    const expected = await imagePixels(join(directory, output.key));
    assert.deepEqual([...output.image.pixels], [...expected.pixels], output.key);
  }
});

test('foreground-composite recipe selects one upstream field per output', async () => {
  const fieldDirectory = 'reference-set/red FG-Alpha';
  const outputDirectory = 'reference-set/Red-col fg-alpha Print/print output';
  const fieldNames = (await readdir(fieldDirectory))
    .filter((filename) => filename.endsWith('.png'))
    .sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
  const outputNames = (await readdir(outputDirectory))
    .filter((filename) => filename.endsWith('.png'))
    .sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
  const archive: ArchiveFixture = {
    files: [
      ...fieldNames.map((filename) => ({ path: `red FG-Alpha/${filename}` })),
      ...outputNames.map((filename) => ({ path: `Red-col fg-alpha Print/print output/${filename}` })),
    ],
  };
  const sources: RasterSources = {};
  for (const filename of fieldNames) sources[Number.parseInt(filename, 10)] = await imagePixels(join(fieldDirectory, filename));
  const result = await compileRecipe(createForegroundCompositeRecipe(archive, { color: '#00ff00' }), {
    resolveRaster: (_assetId: string, asset: RasterAsset) => imagePixels(join('reference-set', asset.path)),
  });
  assert.equal(result.stages.get('foreground-composite-fields')?.length, 22);
  assert.equal(result.outputs.length, 815);
  for (const output of result.outputs) {
    const expected = renderRedPrintArchiveOutput({ filename: output.key, sources, foregroundColour: '#00ff00' });
    assert.deepEqual([...output.image.pixels], [...expected], output.key);
  }
});

test('ordered-result recipe records 960 pixel aliases and 12 raster exceptions', async () => {
  const rgbNames = (await readdir('reference-set/col bin 2/rgb')).filter((filename) => filename.endsWith('.png'));
  const resultNames = (await readdir('reference-set/result')).filter((filename) => filename.endsWith('.png'));
  const archive: ArchiveFixture = {
    files: [
      ...rgbNames.map((filename) => ({ path: `col bin 2/rgb/${filename}` })),
      ...resultNames.map((filename) => ({ path: `result/${filename}` })),
    ],
  };
  const recipe = parseRecipeDocument(createOrderedResultsRecipe(archive));
  assert.equal(recipe.metadata.pixelAliases, 960);
  assert.equal(recipe.metadata.rasterExceptions, 12);
  const aliases = recipe.stages.find((stage) => stage.id === 'ordered-results')?.forEach.alias;
  const exceptions = recipe.stages.find((stage) => stage.id === 'ordered-result-exceptions')?.forEach.file;
  assert.equal(aliases?.source === 'values' ? aliases.values.length : undefined, 960);
  assert.equal(exceptions?.source === 'values' ? exceptions.values.length : undefined, 12);
});

test('custom permutation recipe emits flat, mask, variant, and alias stages', async () => {
  const overlays = [
    { name: 'blk100', colour: '#000000' },
    { name: 'wht100', colour: '#ffffff' },
  ];
  const result = await compileRecipe(createCustomPermutationRecipe([['cyan', '#00ffff']], ['top-down'], overlays));
  assert.equal(result.outputs.length, 6);
  const variants = result.outputs.filter((output) => output.stage === 'custom-variants');
  assert.equal(variants.length, 2);
  assert.deepEqual(
    [...variants[0].image.pixels],
    [...renderBlock({ width: 64, height: 64, base: '#00ffff', overlay: '#000000', opacity: 1, preset: 'top-down', rotation: 0 })],
  );
  const aliases = result.outputs.filter((output) => output.stage === 'custom-results');
  assert.equal(aliases[0].image, variants[0].image);
  assert.equal(aliases[0].path, 'generated/results/Generated_0000.png');
});
