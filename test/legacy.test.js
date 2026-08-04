import assert from 'node:assert/strict';
import test from 'node:test';
import { createCanvas, loadImage } from 'canvas';
import { readdir } from 'node:fs/promises';
import {
  LEGACY_ALPHA_MAP_SPECS,
  MODDING_1900_FLAT_COLOUR,
  RED_FG_ALPHA_SOURCE_SPECS,
  RESULT_GRAD06BLK50_EXCEPTIONS,
  RESULT_VARIANTS,
  compositeSourceOver,
  fillRGBA,
  formatResultFilename,
  getOut4LayerRecipe,
  getRedPrintArchiveRecipe,
  getRedPrintRecipe,
  getResultAliasMapping,
  getResultIndex,
  getResultRecipe,
  parseHistoricVariantName,
  renderEllipticalGradient,
  renderLegacyAlphaMap,
  renderModding1900,
  renderOut4Layer,
  renderRedPrintArchiveOutput,
  renderHistoricBlue8,
  resizeLanczos3RGBA,
  rotateRGBA90,
} from '../src/legacy.js';

async function loadRgba(file) {
  const image = await loadImage(file);
  const canvas = createCanvas(image.width, image.height);
  canvas.getContext('2d').drawImage(image, 0, 0);
  return {
    width: image.width,
    height: image.height,
    pixels: canvas.getContext('2d').getImageData(0, 0, image.width, image.height).data,
  };
}

function difference(actual, expected) {
  assert.equal(actual.length, expected.length);
  let channels = 0;
  let absolute = 0;
  let maximum = 0;
  for (let index = 0; index < actual.length; index += 1) {
    const delta = Math.abs(actual[index] - expected[index]);
    if (delta !== 0) channels += 1;
    absolute += delta;
    maximum = Math.max(maximum, delta);
  }
  return { channels, absolute, maximum };
}

test('legacy maps 00-17 reproduce native dimensions, offsets, and every alpha byte', async () => {
  for (let index = 0; index < 18; index += 1) {
    const fixture = await loadRgba(`reference-set/Gradient Layers Alpha Maps/${String(index).padStart(2, '0')}.png`);
    const generated = renderLegacyAlphaMap(index);
    const spec = LEGACY_ALPHA_MAP_SPECS[index];
    assert.deepEqual([generated.width, generated.height, generated.offsetX, generated.offsetY], [
      fixture.width, fixture.height, spec.offsetX, spec.offsetY,
    ]);
    for (let pixel = 0; pixel < fixture.width * fixture.height; pixel += 1) {
      assert.equal(generated.pixels[pixel * 4 + 3], fixture.pixels[pixel * 4 + 3], `map ${index}, pixel ${pixel}`);
    }
  }
});

test('map 18 is an explicit raster dependency', async () => {
  assert.throws(() => renderLegacyAlphaMap(18), /requires 64x64 RGBA raster data/);
  const fixture = await loadRgba('reference-set/Gradient Layers Alpha Maps/18.png');
  const generated = renderLegacyAlphaMap(18, fixture.pixels);
  assert.deepEqual([...generated.pixels], [...fixture.pixels]);
});

test('out4 first group, including raster map 18 and flat base, is pixel-exact', async () => {
  const map18 = await loadRgba('reference-set/Gradient Layers Alpha Maps/18.png');
  for (let layer = 0; layer < 20; layer += 1) {
    const directory = [6, 7, 8, 15, 16, 17, 18].includes(layer) ? 'out4-special' : 'out4 - Select Library';
    const filename = layer === 0 ? 'Layer_0.png' : `layer_${layer}.png`;
    const fixture = await loadRgba(`reference-set/${directory}/${filename}`);
    const generated = renderOut4Layer({ layerNumber: layer, rasterMap18: map18.pixels });
    assert.deepEqual([...generated], [...fixture.pixels], `out4 layer ${layer}`);
  }
});

test('source-over clips cropped placement and preserves straight RGBA', () => {
  const base = fillRGBA(2, 2, '#0000ff80');
  const source = fillRGBA(2, 1, '#ff000080');
  const result = compositeSourceOver(base, 2, 2, source, 2, 1, { offsetX: 1, offsetY: 0 });
  assert.deepEqual([...result.slice(0, 8)], [0, 0, 255, 128, 170, 0, 85, 192]);
  assert.deepEqual([...result.slice(8)], [0, 0, 255, 128, 0, 0, 255, 128]);
});

test('red foreground-alpha print indexing and a source-over fixture are exact', async () => {
  assert.deepEqual(getRedPrintRecipe(4), { layerNumber: 4, group: 0, base: '#dc8800', sourceNumber: 18 });
  assert.deepEqual(getRedPrintRecipe(21), { layerNumber: 21, group: 0, base: '#dc8800', sourceNumber: 1 });
  assert.deepEqual(getRedPrintRecipe(22), { layerNumber: 22, group: 1, base: '#fcec00', sourceNumber: 22 });

  const source = await loadRgba('reference-set/red FG-Alpha/16.png');
  const fixture = await loadRgba('reference-set/Red-col fg-alpha Print/print output/layer_6.png');
  const normalizedSource = new Uint8ClampedArray(source.pixels);
  for (let offset = 0; offset < normalizedSource.length; offset += 4) {
    if (normalizedSource[offset + 3] === 0) continue;
    normalizedSource[offset] = 255;
    normalizedSource[offset + 1] = 0;
    normalizedSource[offset + 2] = 0;
  }
  const generated = compositeSourceOver(fillRGBA(64, 64, '#dc8800'), 64, 64, normalizedSource, 64, 64);
  assert.deepEqual([...generated], [...fixture.pixels]);
});

test('red print source metadata and archive filename discontinuity are recovered', () => {
  assert.deepEqual(RED_FG_ALPHA_SOURCE_SPECS[9], {
    sourceNumber: 9,
    width: 63,
    height: 63,
    offsetX: 1,
    offsetY: 1,
    rotation: 0,
    normalizeVisibleRgb: [255, 0, 0],
  });
  assert.deepEqual(getRedPrintArchiveRecipe('0.png'), { filename: '0.png', kind: 'red-print', layerNumber: 0 });
  assert.deepEqual(getRedPrintArchiveRecipe('layer_500.png'), { filename: 'layer_500.png', kind: 'red-print', layerNumber: 500 });
  assert.deepEqual(getRedPrintArchiveRecipe('layer_502.png'), { filename: 'layer_502.png', kind: 'red-print', layerNumber: 501 });
  assert.deepEqual(getRedPrintArchiveRecipe('layer_69-1.png'), {
    filename: 'layer_69-1.png', kind: 'flat-alias', colour: '#d4ff00', out4LayerNumber: 780,
  });
});

test('all 815 red print archive outputs are generated within measured source quantization', async (context) => {
  const directory = 'reference-set/Red-col fg-alpha Print/print output';
  const sources = {};
  for (let sourceNumber = 1; sourceNumber <= 22; sourceNumber += 1) {
    sources[sourceNumber] = await loadRgba(`reference-set/red FG-Alpha/${sourceNumber}.png`);
  }
  const files = await readdir(directory);
  let exact = 0;
  let differingChannels = 0;
  let absoluteError = 0;
  let maximumError = 0;
  for (const file of files) {
    const fixture = await loadRgba(`${directory}/${file}`);
    const generated = renderRedPrintArchiveOutput({ filename: file, sources });
    const delta = difference(generated, fixture.pixels);
    if (delta.channels === 0) exact += 1;
    differingChannels += delta.channels;
    absoluteError += delta.absolute;
    maximumError = Math.max(maximumError, delta.maximum);
  }
  context.diagnostic(`${exact}/${files.length} exact; ${differingChannels} differing channels; absolute error ${absoluteError}; max ${maximumError}`);
  assert.equal(files.length, 815);
  assert.equal(exact, 482);
  assert.equal(maximumError, 1);
  assert.equal(absoluteError, differingChannels);
});

test('result numbering recovers all group boundaries and the missing grad06 fixture name', () => {
  assert.equal(RESULT_VARIANTS.length, 81);
  assert.equal(formatResultFilename(0), 'ColBinSet_0000.png');
  assert.equal(formatResultFilename(971), 'ColBinSet_0971.png');
  assert.equal(getResultIndex('col_red_hi', 0), 648);
  assert.deepEqual(getResultRecipe(20), {
    index: 20,
    filename: 'ColBinSet_0020.png',
    groupIndex: 0,
    group: 'col_blue_hi',
    variantIndex: 20,
    variant: 'grad06blk50',
    sourceFilename: 'col_blue_hi-grad06blk50.png',
  });
  assert.equal(getResultRecipe(80).sourceFilename, 'col_blue_hi.png');
  assert.equal(getResultRecipe(81).sourceFilename, 'col_blue_lo-grad00blk25.png');
});

test('all 972 result aliases map to RGB pixels with 12 encoded grad06 exceptions', async (context) => {
  const availableRgb = new Set(await readdir('reference-set/col bin 2/rgb'));
  const exceptions = [];
  let exactAliases = 0;
  for (let index = 0; index < 972; index += 1) {
    const mapping = getResultAliasMapping(index);
    if (!mapping.exactPixelAliasExpected) {
      exceptions.push(mapping);
      assert.equal(availableRgb.has(mapping.rgbPath.split('/').at(-1)), false);
      continue;
    }
    const result = await loadRgba(`reference-set/${mapping.resultPath}`);
    const rgb = await loadRgba(`reference-set/${mapping.rgbPath}`);
    assert.deepEqual([...result.pixels], [...rgb.pixels], `result alias ${index}`);
    exactAliases += 1;
  }
  context.diagnostic(`${exactAliases}/972 exact RGB aliases; ${exceptions.length} missing grad06blk50 exports`);
  assert.equal(exactAliases, 960);
  assert.deepEqual(exceptions.map((entry) => entry.index), RESULT_GRAD06BLK50_EXCEPTIONS.map((entry) => entry.resultIndex));
});

test('historic variant names expose gradient, overlay, opacity, and rotation', () => {
  assert.deepEqual(parseHistoricVariantName('path/col_blue_hi-grad10wht70-rotCCW.png'), {
    filename: 'col_blue_hi-grad10wht70-rotCCW',
    baseName: 'col_blue_hi',
    gradient: 10,
    overlay: 'wht',
    percentage: 70,
    rotation: 'CCW',
  });
  assert.deepEqual(parseHistoricVariantName('col_blue_hi-grad08blk.png'), {
    filename: 'col_blue_hi-grad08blk',
    baseName: 'col_blue_hi',
    gradient: 8,
    overlay: 'blk',
    percentage: null,
    rotation: null,
  });
  assert.equal(parseHistoricVariantName('col_blue_hi.png'), null);
});

test('all 88 historic blue outputs are exact from unrotated 64px sources', async (context) => {
  const smallDirectory = 'reference-set/blue 64-8 24 bit';
  const largeDirectory = 'reference-set/col bin 2/rgb';
  const files = (await readdir(smallDirectory)).filter((file) => file.endsWith('.png'));
  let exact = 0;
  for (const file of files) {
    const unrotatedName = file.replace(/-rot(?:CCW|CW)/, '');
    const sourcePath = unrotatedName.includes('grad06blk50')
      ? 'reference-set/result/ColBinSet_0020.png'
      : `${largeDirectory}/${unrotatedName}`;
    const source = await loadRgba(sourcePath);
    const fixture = await loadRgba(`${smallDirectory}/${file}`);
    const generated = renderHistoricBlue8(source.pixels, file);
    const delta = difference(generated, fixture.pixels);
    assert.equal(delta.channels, 0, file);
    exact += 1;
  }
  context.diagnostic(`${exact}/${files.length} exact after Lanczos3, exact rotation, and sparse archive corrections`);
  assert.equal(files.length, 88);
  assert.equal(exact, 88);
});

test('90-degree rotation is exact for rectangular data and archive CW/CCW files', async () => {
  const rectangular = new Uint8ClampedArray([
    1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255,
    4, 0, 0, 255, 5, 0, 0, 255, 6, 0, 0, 255,
  ]);
  const clockwise = rotateRGBA90(rectangular, 3, 2, 1);
  assert.deepEqual([clockwise.width, clockwise.height], [2, 3]);
  assert.deepEqual([...clockwise.pixels.filter((_, index) => index % 4 === 0)], [4, 1, 5, 2, 6, 3]);

  const original = await loadRgba('reference-set/blue 64-8 24 bit/col_blue_hi-grad10blk.png');
  const cwFixture = await loadRgba('reference-set/blue 64-8 24 bit/col_blue_hi-grad10blk-rotCW.png');
  const ccwFixture = await loadRgba('reference-set/blue 64-8 24 bit/col_blue_hi-grad10blk-rotCCW.png');
  assert.deepEqual([...rotateRGBA90(original.pixels, 8, 8, 1).pixels], [...cwFixture.pixels]);
  assert.deepEqual([...rotateRGBA90(original.pixels, 8, 8, -1).pixels], [...ccwFixture.pixels]);
});

test('elliptical multi-stop gradients support unequal radii, rotation, alpha, and easing', () => {
  const pixels = renderEllipticalGradient({
    width: 5,
    height: 5,
    centerX: 2,
    centerY: 2,
    radiusX: 2,
    radiusY: 1,
    rotation: Math.PI / 2,
    easing: 'legacy',
    stops: [
      { offset: 0, color: '#ffffffff' },
      { offset: 0.5, color: '#ff000080' },
      { offset: 1, color: '#00000000' },
    ],
  });
  assert.deepEqual([...pixels.slice((2 * 5 + 2) * 4, (2 * 5 + 3) * 4)], [255, 255, 255, 255]);
  assert.deepEqual([...pixels.slice((2 * 5) * 4, (2 * 5 + 1) * 4)], [0, 0, 0, 0]);
  assert.deepEqual([...pixels.slice((1 * 5 + 2) * 4, (1 * 5 + 3) * 4)], [255, 0, 0, 128]);
});

test('Lanczos3 downsampling closely tracks all available 64-to-8 blue fixtures', async (context) => {
  const smallDirectory = 'reference-set/blue 64-8 24 bit';
  const largeDirectory = 'reference-set/col bin 2/rgb';
  const files = await readdir(smallDirectory);
  const available = new Set(await readdir(largeDirectory));
  const fixtureFiles = files.filter((file) => file.endsWith('.png') && available.has(file));
  let exact = 0;
  let differingChannels = 0;
  let absoluteError = 0;
  let maximumError = 0;
  for (const file of fixtureFiles) {
    const source = await loadRgba(`${largeDirectory}/${file}`);
    const fixture = await loadRgba(`${smallDirectory}/${file}`);
    const generated = resizeLanczos3RGBA(source.pixels, 64, 64, 8, 8);
    const delta = difference(generated, fixture.pixels);
    if (delta.channels === 0) exact += 1;
    differingChannels += delta.channels;
    absoluteError += delta.absolute;
    maximumError = Math.max(maximumError, delta.maximum);
  }
  context.diagnostic(`${exact}/${fixtureFiles.length} exact; ${differingChannels} differing channels; absolute error ${absoluteError}; max ${maximumError}`);
  assert.equal(fixtureFiles.length, 80);
  assert.ok(exact >= 40);
  assert.ok(maximumError <= 2);
  assert.ok(absoluteError <= 500);
});

test('out4 recipes reject the unrecovered partial group after layer 839', () => {
  assert.deepEqual(getOut4LayerRecipe(839), {
    layerNumber: 839,
    group: 41,
    operation: 19,
    base: '#b37e28',
    mapIndex: null,
    flat: true,
  });
  assert.throws(() => getOut4LayerRecipe(840), /No recovered out4 base/);
});

test('modding 1918 is the only claimed pixel-exact recovered 1900 output', async () => {
  assert.equal(MODDING_1900_FLAT_COLOUR, '#ffb5a3');
  const fixture = await loadRgba('reference-set/out4 - modding/1918.png');
  assert.deepEqual([...renderModding1900(1918)], [...fixture.pixels]);
  assert.throws(() => renderModding1900(1917), /not analytically recovered/);
});
