import assert from 'node:assert/strict';
import test from 'node:test';
import { errorSummary, recipeProjectionTargets, STARTER_RECIPE, STARTER_SOURCE } from '../src/binscript-notebook.js';
import { compileRecipe } from '../src/recipe-executor.js';

test('starter notebook recipe compiles dependent stages into four outputs', async () => {
  const result = await compileRecipe(STARTER_RECIPE);
  assert.deepEqual(
    [...result.stages].map(([stage, artifacts]) => [stage, artifacts.length]),
    [
      ['base-colors', 2],
      ['gradient-fields', 2],
      ['masked-tiles', 4],
    ],
  );
  assert.equal(result.outputs.length, 4);
});

test('notebook projections retain exact source ranges for controls and stages', () => {
  const targets = recipeProjectionTargets(STARTER_SOURCE);
  const stages = targets.filter((target) => target.kind === 'stage');
  const colors = targets.filter((target) => target.kind === 'color');
  const numbers = targets.filter((target) => target.kind === 'number');

  assert.deepEqual(
    stages.map((target) => target.stageId),
    ['base-colors', 'gradient-fields', 'masked-tiles'],
  );
  assert.deepEqual(
    colors.map((target) => STARTER_SOURCE.slice(target.from, target.to)),
    ['"#ff6030"', '"#10d9d2"', '"#ffffff"'],
  );
  assert.equal(numbers.length, 5);
  for (const target of targets) assert.ok(target.from >= 0 && target.to <= STARTER_SOURCE.length && target.from < target.to);
  for (const target of stages) assert.ok(target.at === STARTER_SOURCE.length || STARTER_SOURCE[target.at - 1] === '\n');
});

test('notebook reports the first schema issue as a readable status', () => {
  assert.equal(
    errorSummary({ issues: [{ path: ['stages', 0, 'image'], message: 'Invalid image operation' }] }),
    'stages.0.image: Invalid image operation',
  );
});
