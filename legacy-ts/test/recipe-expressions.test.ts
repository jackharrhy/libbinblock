import assert from 'node:assert/strict';
import test from 'node:test';
import { evaluateExpression } from '../src/recipe-expressions.js';
import type { Expression } from '../src/recipe-schema.js';

test('Mapbox-style expressions resolve variables, object properties, and literal arrays', () => {
  const context = {
    variables: { color: { id: 'blue', value: '#0000ff' } },
    properties: { fallback: 'unknown' },
  };
  assert.equal(evaluateExpression(['get', 'value', ['var', 'color']], context), '#0000ff');
  assert.equal(evaluateExpression(['get', 'fallback'], context), 'unknown');
  assert.deepEqual(evaluateExpression(['literal', [64, 64]], context), [64, 64]);
});

test('case, match, let, and deterministic string operators compose', () => {
  const expression: Expression = [
    'let',
    'number',
    7,
    [
      'concat',
      ['match', ['var', 'number'], 7, 'layer-', 'other-'],
      ['pad', ['to-string', ['var', 'number']], 3, '0'],
      ['case', ['==', ['var', 'number'], 7], '.png', '.bin'],
    ],
  ];
  assert.equal(evaluateExpression(expression), 'layer-007.png');
});

test('numeric expressions reject division by zero', () => {
  assert.throws(() => evaluateExpression(['/', 2, 0]), /Division by zero/);
});
