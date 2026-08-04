import assert from 'node:assert/strict';
import test from 'node:test';
import { createCanvas, loadImage } from 'canvas';
import { readFile } from 'node:fs/promises';
import { renderBlock, renderMask } from '../src/core.js';

function rgbaFromCanvas(canvas) {
  return canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
}

test('solid blocks render exact 8-bit RGB values', () => {
  const pixels = renderBlock({ width: 2, height: 2, base: '#12abef', overlay: '#000000', opacity: 0 });
  assert.deepEqual([...pixels], [18, 171, 239, 255, 18, 171, 239, 255, 18, 171, 239, 255, 18, 171, 239, 255]);
});

test('gradient channels use deterministic integer rounding', () => {
  const pixels = renderBlock({ width: 3, height: 1, base: '#0000ff', overlay: '#000000', opacity: 1, preset: 'left-right' });
  assert.deepEqual([...pixels], [0, 0, 0, 255, 0, 0, 128, 255, 0, 0, 255, 255]);
});

test('alpha masks retain transparent RGB data for archive export', () => {
  const pixels = renderMask({ width: 1, height: 2, preset: 'top-down' });
  assert.deepEqual([...pixels], [0, 0, 0, 255, 0, 0, 0, 0]);
});

test('reference-set solid blue fixture is pixel-identical to the shared renderer', async () => {
  const fixture = 'reference-set/blue 64-8 24 bit/col_blue_hi.png';
  await readFile(fixture);
  const image = await loadImage(fixture);
  const canvas = createCanvas(image.width, image.height);
  canvas.getContext('2d').drawImage(image, 0, 0);
  const actual = rgbaFromCanvas(canvas);
  const expected = renderBlock({ width: image.width, height: image.height, base: '#0000ff', opacity: 0 });
  assert.deepEqual([...actual], [...expected]);
});
