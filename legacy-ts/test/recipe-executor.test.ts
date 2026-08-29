import assert from 'node:assert/strict';
import test from 'node:test';
import { compileRecipe } from '../src/recipe-executor.js';
import type { JsonValue, RecipeDocument } from '../src/recipe-schema.js';

interface ColorFixture {
  [key: string]: JsonValue;
  id: string;
  value: string;
}

const COLORS: ColorFixture[] = [
  { id: 'red', value: '#ff0000' },
  { id: 'blue', value: '#0000ff' },
];

function stagedRecipe(): RecipeDocument {
  return {
    format: 'bin-block-recipe/v1',
    profile: 'numeric-srgb/v1',
    metadata: {},
    values: {},
    assets: {},
    definitions: {
      'colored-block': {
        parameters: { color: '#000000' },
        image: { op: 'fill', width: 2, height: 2, color: ['var', 'color'] },
      },
    },
    stages: [
      {
        type: 'render',
        id: 'downscaled',
        forEach: { source: { source: 'stage', stage: 'base-colors' } },
        key: ['get', 'key', ['var', 'source']],
        path: ['concat', 'downscaled/', ['get', 'key', ['var', 'source']], '.png'],
        properties: { sourceStage: ['get', 'stage', ['var', 'source']] },
        image: { op: 'resize', source: { op: 'input', binding: 'source' }, width: 1, height: 1, filter: 'lanczos3' },
      },
      {
        type: 'render',
        id: 'base-colors',
        forEach: { color: { source: 'values', values: COLORS } },
        key: ['get', 'id', ['var', 'color']],
        properties: { color: ['get', 'value', ['var', 'color']] },
        image: {
          op: 'use',
          definition: 'colored-block',
          with: { color: ['get', 'value', ['var', 'color']] },
        },
      },
      {
        type: 'alias',
        id: 'aliases',
        forEach: { target: { source: 'stage', stage: 'downscaled' } },
        target: { binding: 'target' },
        identity: 'pixels',
        key: ['get', 'key', ['var', 'target']],
        path: ['concat', 'aliases/', ['get', 'key', ['var', 'target']], '.png'],
        properties: {},
      },
    ],
    outputs: [{ stage: 'downscaled' }, { stage: 'aliases' }],
  };
}

test('stages consume upstream artifact sets in stable dependency order', async () => {
  const result = await compileRecipe(stagedRecipe());
  assert.deepEqual(
    result.outputs.map((output) => output.path),
    ['downscaled/red.png', 'downscaled/blue.png', 'aliases/red.png', 'aliases/blue.png'],
  );
  assert.deepEqual([...result.outputs[0].image.pixels], [255, 0, 0, 255]);
  assert.deepEqual([...result.outputs[1].image.pixels], [0, 0, 255, 255]);
  assert.deepEqual(result.outputs[0].properties, { sourceStage: 'base-colors' });
  assert.deepEqual(result.outputs[2].provenance, { type: 'alias', identity: 'pixels', target: 'downscaled/red' });
  assert.equal(result.outputs[2].image, result.outputs[0].image);
});

test('multiple upstream stage axes form a deterministic Cartesian product', async () => {
  const recipe = stagedRecipe();
  recipe.stages.splice(2, 0, {
    type: 'render',
    id: 'combinations',
    forEach: {
      left: { source: 'stage', stage: 'base-colors' },
      right: { source: 'stage', stage: 'downscaled' },
    },
    key: ['concat', ['get', 'key', ['var', 'left']], '-', ['get', 'key', ['var', 'right']]],
    path: ['concat', 'combinations/', ['get', 'key', ['var', 'left']], '-', ['get', 'key', ['var', 'right']], '.png'],
    properties: {},
    image: {
      op: 'composite',
      destination: { op: 'input', binding: 'left' },
      source: { op: 'input', binding: 'right' },
      offsetX: 0,
      offsetY: 0,
      opacity: 0.5,
    },
  });
  recipe.outputs = [{ stage: 'combinations' }];
  const result = await compileRecipe(recipe);
  assert.deepEqual(
    result.outputs.map((output) => output.key),
    ['red-red', 'red-blue', 'blue-red', 'blue-blue'],
  );
});

test('output paths and artifact counts are guarded during compilation', async () => {
  const traversal = stagedRecipe();
  traversal.stages[0].path = '../escape.png';
  await assert.rejects(() => compileRecipe(traversal), /cannot contain/);
  await assert.rejects(() => compileRecipe(stagedRecipe(), { maximumArtifacts: 1 }), /artifact limit/);
});

test('stage bindings shadow document values with the same name', async () => {
  const recipe = stagedRecipe();
  recipe.values = { color: { id: 'wrong', value: '#000000' } };
  const result = await compileRecipe(recipe);
  assert.deepEqual(
    result.stages.get('base-colors')?.map((artifact) => artifact.key),
    ['red', 'blue'],
  );
});

test('stage axes filter upstream artifact sets using earlier bindings', async () => {
  const recipe = stagedRecipe();
  recipe.stages.splice(2, 0, {
    type: 'alias',
    id: 'selected-aliases',
    forEach: {
      wanted: { source: 'values', values: ['blue'] },
      target: {
        source: 'stage',
        stage: 'downscaled',
        where: ['==', ['get', 'key', ['var', 'target']], ['var', 'wanted']],
      },
    },
    target: { binding: 'target' },
    key: ['get', 'key', ['var', 'target']],
    path: ['concat', 'selected/', ['get', 'key', ['var', 'target']], '.png'],
    identity: 'pixels',
    properties: {},
  });
  recipe.outputs = [{ stage: 'selected-aliases' }];
  const result = await compileRecipe(recipe);
  assert.deepEqual(
    result.outputs.map((output) => output.key),
    ['blue'],
  );
});
