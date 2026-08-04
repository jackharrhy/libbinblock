import assert from 'node:assert/strict';
import test from 'node:test';
import { BinScriptError, compileBinScript } from '../src/binscript-language.js';
import { errorSummary, recipeProjectionTargets, STARTER_RECIPE, STARTER_SOURCE } from '../src/binscript-notebook.js';
import { compileRecipe } from '../src/recipe-executor.js';

test('starter BinScript lowers and compiles dependent image bindings', async () => {
  const result = await compileRecipe(STARTER_RECIPE);
  assert.deepEqual(
    [...result.stages].map(([stage, artifacts]) => [stage, artifacts.length]),
    [
      ['blocks', 2],
      ['light', 1],
      ['tiles', 2],
    ],
  );
  assert.equal(result.outputs.length, 5);
});

test('notebook projections retain exact source ranges for controls and stages', () => {
  const targets = recipeProjectionTargets(STARTER_SOURCE);
  const stages = targets.filter((target) => target.kind === 'stage');
  const colors = targets.filter((target) => target.kind === 'color');
  const numbers = targets.filter((target) => target.kind === 'number');

  assert.deepEqual(
    stages.map((target) => target.stageId),
    ['blocks', 'light', 'tiles'],
  );
  assert.deepEqual(
    colors.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['#ff6030', '#10d9d2', '#ffffff'],
  );
  assert.deepEqual(
    numbers.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['64', '135deg'],
  );
  for (const target of targets) assert.ok(target.from >= 0 && target.to <= STARTER_SOURCE.length && target.from < target.to);
  for (const target of stages) assert.ok(target.at === STARTER_SOURCE.length || STARTER_SOURCE[target.at - 1] === '\n');
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
