import assert from 'node:assert/strict';
import test from 'node:test';
import { parseRecipeDocument, recipeJsonSchema, RecipeDocumentSchema } from '../src/recipe-schema.js';

function minimalRecipe(overrides = {}) {
  return {
    format: 'bin-block-recipe/v1',
    stages: [{
      type: 'render',
      id: 'solid',
      forEach: {},
      key: 'solid',
      path: 'solid.png',
      image: { op: 'fill', width: 1, height: 1, color: '#112233' },
    }],
    outputs: [{ stage: 'solid' }],
    ...overrides,
  };
}

test('recipe parsing applies closed-schema defaults', () => {
  const recipe = parseRecipeDocument(minimalRecipe());
  assert.equal(recipe.profile, 'numeric-srgb/v1');
  assert.deepEqual(recipe.assets, {});
  assert.deepEqual(recipe.definitions, {});
  assert.deepEqual(recipe.stages[0].properties, {});
});

test('recipe Zod definitions export as JSON Schema for editors and tooling', () => {
  const schema = recipeJsonSchema();
  assert.equal(schema.$schema, 'https://json-schema.org/draft/2020-12/schema');
  assert.equal(schema.type, 'object');
  assert.equal(schema.additionalProperties, false);
});

test('recipe schema rejects unknown operations and fields', () => {
  const operation = minimalRecipe();
  operation.stages[0].image = { op: 'magic-filter', amount: 1 };
  assert.equal(RecipeDocumentSchema.safeParse(operation).success, false);

  const unknownField = minimalRecipe({ surprise: true });
  assert.equal(RecipeDocumentSchema.safeParse(unknownField).success, false);
});

test('recipe schema reports missing stages, input bindings, assets, and dependency cycles', () => {
  const recipe = minimalRecipe({
    stages: [
      {
        type: 'render', id: 'first', forEach: { second: { source: 'stage', stage: 'second' } }, key: 'first',
        image: { op: 'input', binding: 'missing' },
      },
      {
        type: 'render', id: 'second', forEach: { first: { source: 'stage', stage: 'first' } }, key: 'second',
        image: { op: 'raster', asset: 'missing-raster' },
      },
    ],
    outputs: [{ stage: 'missing-output' }],
  });
  const result = RecipeDocumentSchema.safeParse(recipe);
  assert.equal(result.success, false);
  const messages = result.error.issues.map((issue) => issue.message).join('\n');
  assert.match(messages, /Unknown input binding/);
  assert.match(messages, /Unknown raster asset/);
  assert.match(messages, /Unknown output stage/);
  assert.match(messages, /dependency cycle/);
});

test('recipe schema validates reusable definition references and cycles', () => {
  const recipe = minimalRecipe({
    definitions: {
      first: { parameters: {}, image: { op: 'use', definition: 'second', with: {} } },
      second: { parameters: {}, image: { op: 'use', definition: 'first', with: {} } },
      broken: { parameters: {}, image: { op: 'raster', asset: 'missing' } },
    },
  });
  const result = RecipeDocumentSchema.safeParse(recipe);
  assert.equal(result.success, false);
  const messages = result.error.issues.map((issue) => issue.message).join('\n');
  assert.match(messages, /Definition cycle/);
  assert.match(messages, /Unknown raster asset/);
});
