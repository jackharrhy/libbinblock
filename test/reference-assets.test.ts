import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import assert from 'node:assert/strict';
import manifest from '../reference-manifest.json' with { type: 'json' };
import { decodeReferencePng } from '../bindings/javascript/reference-assets.js';

test('browser reference decoder preserves the locked RGBA8 contract', async () => {
  const record = manifest.files.find((file) => file.path === 'Gradient Layers Alpha Maps/00.png');
  assert.ok(record);
  const encoded = await readFile(new URL(`../reference-set/${record.path}`, import.meta.url));
  const image = await decodeReferencePng(encoded);
  assert.equal(image.width, record.width);
  assert.equal(image.height, record.height);
  assert.equal(createHash('sha256').update(image.pixels).digest('hex'), record.decodedRgba8Sha256);
});
