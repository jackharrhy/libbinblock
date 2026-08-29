import assert from 'node:assert/strict';
import test from 'node:test';
import { createCanvas, loadImage } from 'canvas';
import { BinScriptError, compileBinScript } from '../legacy-ts/src/binscript-language.js';
import { errorSummary, recipeProjectionTargets, STARTER_RECIPE, STARTER_SOURCE } from '../legacy-ts/src/binscript-notebook.js';
import { DEFAULT_PALETTE } from '../legacy-ts/src/core.js';
import { compileRecipe } from '../legacy-ts/src/recipe-executor.js';

async function fixturePixels(path: string): Promise<{ width: number; height: number; pixels: Uint8ClampedArray }> {
  const image = await loadImage(path);
  const canvas = createCanvas(image.width, image.height);
  const context = canvas.getContext('2d');
  context.drawImage(image, 0, 0);
  return { width: image.width, height: image.height, pixels: context.getImageData(0, 0, image.width, image.height).data };
}

function compileStarter() {
  return compileRecipe(STARTER_RECIPE);
}

const BLUE_HIGH_VARIANT_NAMES = [
  'grad00blk100',
  'grad00blk25',
  'grad00wht',
  'grad01blk100',
  'grad01blk25',
  'grad01wht',
  'grad02blk100',
  'grad02blk50',
  'grad02wht',
  'grad03blk100',
  'grad03blk25',
  'grad03blk50',
  'grad03wht100',
  'grad04blk100',
  'grad04blk50',
  'grad04wht',
] as const;

test('starter BinScript lowers and compiles dependent image bindings', async () => {
  const result = await compileStarter();
  assert.equal(result.stages.get('blocks')?.length, 16);
  for (let index = 0; index < 19; index += 1) assert.equal(result.stages.get(`mask-${String(index).padStart(2, '0')}`)?.length, 1);
  assert.equal(
    [...result.stages]
      .filter(([stage]) => stage.startsWith('alpha-variants-'))
      .reduce((total, [, artifacts]) => total + artifacts.length, 0),
    304,
  );
  assert.equal(
    [...result.stages]
      .filter(([stage]) => stage.startsWith('small-alpha-variants-'))
      .reduce((total, [, artifacts]) => total + artifacts.length, 0),
    304,
  );
  for (const name of BLUE_HIGH_VARIANT_NAMES) assert.equal(result.stages.get(`blue-hi-${name}`)?.length, 1);
  assert.equal(result.outputs.length, 663);
  assert.deepEqual(
    result.stages.get('blocks')?.map((artifact) => [artifact.key, artifact.properties.color]),
    DEFAULT_PALETTE.map(([id, color]) => [id.replace(/^col_/, '').replaceAll('_', '-'), color]),
  );
});

test('starter cardinal gradients run in their named directions', async () => {
  const result = await compileStarter();
  const alpha = (stage: string, x: number, y: number): number => {
    const image = result.stages.get(stage)?.[0]?.image;
    assert.ok(image);
    return image.pixels[(y * image.width + x) * 4 + 3];
  };

  assert.deepEqual([alpha('top-down', 32, 0), alpha('top-down', 32, 63)], [255, 0]);
  assert.deepEqual([alpha('bottom-up', 32, 0), alpha('bottom-up', 32, 63)], [0, 255]);
  assert.deepEqual([alpha('left-right', 0, 32), alpha('left-right', 63, 32)], [255, 0]);
  assert.deepEqual([alpha('right-left', 0, 32), alpha('right-left', 63, 32)], [0, 255]);
});

test('notebook projections retain exact source ranges for controls and stages', () => {
  const targets = recipeProjectionTargets(STARTER_SOURCE);
  const stages = targets.filter((target) => target.kind === 'stage');
  const comparisons = targets.filter((target) => target.kind === 'comparison');
  const colors = targets.filter((target) => target.kind === 'color');
  const numbers = targets.filter((target) => target.kind === 'number');

  assert.deepEqual(
    stages.map((target) => target.stageRows),
    [
      [['blocks']],
      [['bottom-up', 'top-down', 'left-right', 'right-left']],
      [
        [
          'mask-00',
          'mask-01',
          'mask-02',
          'placed-03',
          'mask-04-placed',
          'mask-05',
          'mask-06-placed',
          'mask-07',
          'placed-08',
          'mask-09',
          'placed-10',
          'mask-11',
          'placed-12',
          'placed-13',
          'mask-14',
          'mask-15',
          'placed-16',
          'placed-17',
          'mask-18',
        ],
      ],
      [BLUE_HIGH_VARIANT_NAMES.map((name) => `blue-hi-${name}`)],
      Array.from({ length: 19 }, (_, index) => [`alpha-variants-${index + 1}`]),
      Array.from({ length: 19 }, (_, index) => [`small-alpha-variants-${index + 1}`]),
    ],
  );
  assert.deepEqual(
    comparisons.map((target) => target.stageRows),
    [
      [Array.from({ length: 19 }, (_, index) => `mask-${String(index).padStart(2, '0')}`)],
      [BLUE_HIGH_VARIANT_NAMES.map((name) => `blue-hi-${name}`)],
    ],
  );
  assert.deepEqual(
    colors.slice(0, DEFAULT_PALETTE.length).map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    DEFAULT_PALETTE.map(([, color]) => color),
  );
  assert.deepEqual(
    colors.slice(DEFAULT_PALETTE.length).map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['white', 'transparent-white', 'black', 'white', 'transparent-black', '#ffffff70', '#ffffff00', '#000000ff', '#00000000', '#0000ff'],
  );
  assert.deepEqual(
    numbers.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['64', '180deg', '0deg', '90deg', '270deg', '180deg', '63', '62'],
  );
  for (const target of targets) assert.ok(target.from >= 0 && target.to <= STARTER_SOURCE.length && target.from < target.to);
  for (const target of stages) assert.equal(target.at, target.to);
});

test('notebook reports the first schema issue as a readable status', () => {
  assert.equal(
    errorSummary({ issues: [{ path: ['stages', 0, 'image'], message: 'Invalid image operation' }] }),
    'stages.0.image: Invalid image operation',
  );
});

test('BinScript supports implicit basic bindings, gradient shorthands, and expression previews', async () => {
  const source = `colors := palette(red: #ff0000, blue: #0000ff)
blocks := colors.map(fill).size(8, 4)
fade := rg(circle, white, transparent).size(8, 4)
result := blocks.mask(fade, multiply)
result`;
  const compiled = compileBinScript(source);
  const result = await compileRecipe(compiled.document);
  assert.deepEqual(
    [...result.stages].map(([stage, artifacts]) => [stage, artifacts.length]),
    [
      ['blocks', 2],
      ['fade', 1],
      ['result', 2],
    ],
  );
  assert.deepEqual(
    result.outputs.map((artifact) => [artifact.image.width, artifact.image.height]),
    [
      [8, 4],
      [8, 4],
    ],
  );
});

test('BinScript syntax errors retain their exact source range', () => {
  const source = 'block := fill(#ff0000).size(';
  assert.throws(
    () => compileBinScript(source),
    (error: unknown) =>
      error instanceof BinScriptError &&
      error.message === 'Expected a closing parenthesis.' &&
      error.from === source.length &&
      error.to === source.length + 1,
  );
});

test('BinScript distributes omitted CSS gradient stops between explicit positions', () => {
  const compiled = compileBinScript('fade := linear-gradient(90deg, #000000 20%, #444444, #888888 40%, #ffffff).size(8)\nfade.preview()');
  const image = compiled.document.stages[0].type === 'render' ? compiled.document.stages[0].image : undefined;
  assert.ok(image?.op === 'gradient' && image.shape === 'linear');
  assert.deepEqual(
    image.stops.map((stop) => Number(Number(stop.offset).toFixed(6))),
    [0.2, 0.3, 0.4, 1],
  );
});

test('BinScript preserves transparent RGB and projects named colors as controls', () => {
  const source = `white-fade := lg(90deg, white, transparent-white)
black-fade := lg(90deg, white, transparent-black)
css-fade := lg(90deg, white, transparent)`;
  const compiled = compileBinScript(source);
  const colors = compiled.projections.filter((target) => target.kind === 'color');

  assert.deepEqual(
    colors.map((target) => [source.slice(target.from, target.to), target.value]),
    [
      ['white', '#ffffff'],
      ['transparent-white', '#ffffff00'],
      ['white', '#ffffff'],
      ['transparent-black', '#00000000'],
      ['white', '#ffffff'],
      ['transparent', '#00000000'],
    ],
  );
  assert.deepEqual(
    compiled.document.stages.map((stage) =>
      stage.type === 'render' && stage.image.op === 'gradient' && stage.image.shape !== 'preset'
        ? stage.image.stops.map((stop) => stop.color)
        : [],
    ),
    [
      ['#ffffff', '#ffffff00'],
      ['#ffffff', '#00000000'],
      ['#ffffff', '#00000000'],
    ],
  );
});

test('BinScript color bindings can be reused across image operations', () => {
  const compiled = compileBinScript(`gradient-start := white
gradient-end := transparent-white
horizontal := lg(90deg, gradient-start, gradient-end)
vertical := lg(180deg, gradient-start, gradient-end)`);

  assert.deepEqual(
    compiled.document.stages.map((stage) =>
      stage.type === 'render' && stage.image.op === 'gradient' && stage.image.shape !== 'preset'
        ? stage.image.stops.map((stop) => stop.color)
        : [],
    ),
    [
      ['#ffffff', '#ffffff00'],
      ['#ffffff', '#ffffff00'],
    ],
  );
  assert.equal(compiled.projections.filter((target) => target.kind === 'color').length, 2);
});

test('BinScript rejects extra fluent arguments instead of silently ignoring them', () => {
  assert.throws(
    () => compileBinScript('block := fill(#ff0000).size(8)\nblock.preview(extra)'),
    (error: unknown) => error instanceof BinScriptError && error.message === 'preview() does not accept arguments.',
  );
});

test('BinScript bindings stay pure until referenced as standalone expressions', () => {
  const hidden = compileBinScript('block := fill(#ff0000).size(8)');
  assert.equal(
    hidden.projections.some((target) => target.kind === 'stage'),
    false,
  );

  const displayed = compileBinScript('block := fill(#ff0000).size(8)\nblock');
  const stages = displayed.projections.filter((target) => target.kind === 'stage');
  assert.equal(stages.length, 1);
  assert.equal(displayed.document.outputs[0].stage, 'block');
});

test('BinScript arrays preserve nested image-set order in one display projection', () => {
  const compiled = compileBinScript(`red := fill(#ff0000)
blue := fill(#0000ff)
pair := [red, blue]
[pair, red]`);
  assert.deepEqual(compiled.document.outputs, [{ stage: 'red' }, { stage: 'blue' }]);
  const displays = compiled.projections.filter((target) => target.kind === 'stage');
  assert.deepEqual(
    displays.map((target) => target.stageRows),
    [[['red', 'blue'], ['red']]],
  );
});

test('BinScript preambles use explicit import path strings', () => {
  assert.doesNotThrow(() => compileBinScript('import "bingen/basic"\nblock := fill(#ff0000)\nblock'));
  assert.throws(
    () => compileBinScript('import basic\nblock := fill(#ff0000)\nblock'),
    (error: unknown) => error instanceof BinScriptError && error.message === 'Expected an import path string.',
  );
});

test('BinScript numeric bindings can drive multiple image parameters', async () => {
  const compiled = compileBinScript(`size := 12
red := fill(#ff0000).size(size)
blue := fill(#0000ff).size(size)
red
blue`);
  const result = await compileRecipe(compiled.document);
  assert.deepEqual(
    result.outputs.map((artifact) => [artifact.image.width, artifact.image.height]),
    [
      [12, 12],
      [12, 12],
    ],
  );
  const numberTargets = compiled.projections.filter((target) => target.kind === 'number');
  assert.equal(numberTargets.length, 1);
  assert.equal(numberTargets[0].property, 'size');
});

test('BinScript recursively materializes collection masks and transforms', async () => {
  const compiled = compileBinScript(`colors := palette(red: #ff0000, blue: #0000ff)
blocks := colors.map(fill).size(8)
horizontal := lg(90deg, white, transparent).size(8)
vertical := lg(180deg, white, transparent).size(8)
gradients := collect([horizontal, vertical])
variants := blocks.mask(gradients)
resized := variants.resize(4)
small := resized.opacity(0.5)
small`);
  const result = await compileRecipe(compiled.document);

  assert.deepEqual(
    [...result.stages].map(([stage, artifacts]) => [stage, artifacts.length]),
    [
      ['blocks', 2],
      ['horizontal', 1],
      ['vertical', 1],
      ['variants-1', 2],
      ['variants-2', 2],
      ['resized-1', 2],
      ['resized-2', 2],
      ['small-1', 2],
      ['small-2', 2],
    ],
  );
  assert.equal(result.outputs.length, 4);
  for (const artifact of result.outputs) {
    assert.deepEqual([artifact.image.width, artifact.image.height], [4, 4]);
    assert.ok(artifact.image.pixels.some((channel, index) => index % 4 === 3 && channel > 0 && channel < 255));
  }
});

test('BinScript 64-to-8 resize is pixel-exact against the blue reference fixture', async () => {
  const compiled = compileBinScript(`blue := fill(#0000ff).size(64)
small := blue.resize(8)
small`);
  const result = await compileRecipe(compiled.document);
  const generated = result.outputs[0];
  const expected = await fixturePixels('reference-set/blue 64-8 24 bit/col_blue_hi.png');

  assert.ok(generated);
  assert.deepEqual([generated.image.width, generated.image.height], [expected.width, expected.height]);
  assert.deepEqual([...generated.image.pixels], [...expected.pixels]);
});

test('visible starter mask bindings reproduce the reference fixtures', async () => {
  const result = await compileStarter();
  for (let index = 0; index < 19; index += 1) {
    const key = String(index).padStart(2, '0');
    const generated = result.stages.get(`mask-${key}`)?.[0]?.image;
    const expected = await fixturePixels(`reference-set/Gradient Layers Alpha Maps/${key}.png`);
    assert.ok(generated, key);
    assert.deepEqual([generated.width, generated.height], [expected.width, expected.height], key);
    if (index === 18) {
      let squaredError = 0;
      let maximumError = 0;
      for (let channel = 0; channel < generated.pixels.length; channel += 1) {
        const error = Math.abs(generated.pixels[channel] - expected.pixels[channel]);
        squaredError += error ** 2;
        maximumError = Math.max(maximumError, error);
      }
      assert.ok(Math.sqrt(squaredError / generated.pixels.length) < 2, `18 RMSE exceeds two bytes`);
      assert.ok(maximumError <= 7, `18 maximum error exceeds seven bytes`);
    } else {
      assert.deepEqual(
        [...generated.pixels].filter((_value, channel) => channel % 4 === 3),
        [...expected.pixels].filter((_value, channel) => channel % 4 === 3),
        key,
      );
    }
  }
});

test('visible starter radial fields reproduce the first historic blue-high variant slice', async () => {
  const result = await compileStarter();
  for (const name of BLUE_HIGH_VARIANT_NAMES) {
    const generated = result.stages.get(`blue-hi-${name}`)?.[0]?.image;
    const expected = await fixturePixels(`reference-set/col bin 2/rgb/col_blue_hi-${name}.png`);
    assert.ok(generated, name);
    assert.deepEqual([generated.width, generated.height], [expected.width, expected.height], name);
    let maximumError = 0;
    for (let channel = 0; channel < generated.pixels.length; channel += 1) {
      maximumError = Math.max(maximumError, Math.abs(generated.pixels[channel] - expected.pixels[channel]));
    }
    assert.ok(maximumError <= 2, `${name} maximum error exceeds two bytes`);
  }
});

test('public starter masks normalize historical crops to 64 by 64', async () => {
  const result = await compileStarter();
  const maskProjection = recipeProjectionTargets(STARTER_SOURCE).filter((target) => target.kind === 'stage')[2];
  assert.ok(maskProjection?.kind === 'stage');
  const stageIds = maskProjection.stageRows.flat();
  assert.equal(stageIds.length, 19);
  for (const stageId of stageIds) {
    const image = result.stages.get(stageId)?.[0]?.image;
    assert.ok(image, stageId);
    assert.deepEqual([image.width, image.height], [64, 64], stageId);
  }
});
