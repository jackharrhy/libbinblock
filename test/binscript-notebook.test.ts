import assert from 'node:assert/strict';
import test from 'node:test';
import { BinScriptError, compileBinScript } from '../src/binscript-language.js';
import { errorSummary, recipeProjectionTargets, STARTER_RECIPE, STARTER_SOURCE } from '../src/binscript-notebook.js';
import { DEFAULT_PALETTE } from '../src/core.js';
import { compileRecipe } from '../src/recipe-executor.js';

test('starter BinScript lowers and compiles dependent image bindings', async () => {
  const result = await compileRecipe(STARTER_RECIPE);
  assert.deepEqual(
    [...result.stages].map(([stage, artifacts]) => [stage, artifacts.length]),
    [
      ['blocks', 16],
      ['top-down', 1],
      ['top-down-colors', 16],
      ['bottom-up', 1],
      ['bottom-up-colors', 16],
      ['left-right', 1],
      ['left-right-colors', 16],
      ['right-left', 1],
      ['right-left-colors', 16],
    ],
  );
  assert.equal(result.outputs.length, 84);
  assert.deepEqual(
    result.stages.get('blocks')?.map((artifact) => [artifact.key, artifact.properties.color]),
    DEFAULT_PALETTE.map(([id, color]) => [id.replaceAll('_', '-'), color]),
  );
});

test('starter cardinal gradients run in their named directions', async () => {
  const result = await compileRecipe(STARTER_RECIPE);
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
  const colors = targets.filter((target) => target.kind === 'color');
  const numbers = targets.filter((target) => target.kind === 'number');

  assert.deepEqual(
    stages.map((target) => target.stageId),
    [
      'blocks',
      'top-down',
      'top-down-colors',
      'bottom-up',
      'bottom-up-colors',
      'left-right',
      'left-right-colors',
      'right-left',
      'right-left-colors',
    ],
  );
  assert.deepEqual(
    colors.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    DEFAULT_PALETTE.map(([, color]) => color),
  );
  assert.deepEqual(
    numbers.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['64', '180deg', '0deg', '90deg', '270deg'],
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
