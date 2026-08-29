import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { decodeReferencePng, generateReferenceManifest, serializeReferenceManifest } from '../scripts/reference-manifest.js';

test('reference manifest is deterministic and locked to the complete archive', async () => {
  const first = serializeReferenceManifest(await generateReferenceManifest());
  const second = serializeReferenceManifest(await generateReferenceManifest());
  assert.equal(second, first);
  assert.equal(await readFile('reference-manifest.json', 'utf8'), first);

  const manifest = JSON.parse(first) as Awaited<ReturnType<typeof generateReferenceManifest>>;
  assert.equal(manifest.fileCount, 4312);
  assert.equal(
    manifest.families.reduce((total, family) => total + family.imageCount, 0),
    4312,
  );
  assert.equal(manifest.files.filter((file) => file.family === 'ordered-results' && file.equivalence === 'pixel-alias').length, 960);
  assert.equal(manifest.files.filter((file) => file.family === 'ordered-results' && file.equivalence === 'raster-fallback').length, 12);
});

test('reference PNG decoder preserves stored RGB beneath transparent pixels', async () => {
  const bytes = await readFile('reference-set/Gradient Layers Alpha Maps/04.png');
  const decoded = decodeReferencePng(bytes);
  assert.equal(decoded.width, 63);
  assert.equal(decoded.height, 63);
  assert.equal(decoded.pixelFormat, 'RGBA8_UNORM');
  assert.equal(decoded.alphaPresence, 'translucent');
  const transparentPixel = decoded.rgba.findIndex((value, index) => index % 4 === 3 && value === 0);
  assert.notEqual(transparentPixel, -1);
  assert.deepEqual([...decoded.rgba.subarray(transparentPixel - 3, transparentPixel)], [255, 255, 255]);
});

test('curated smoke fixtures cover every archive family and known exception shapes', async () => {
  const smoke = JSON.parse(await readFile('tests/fixtures/reference-smoke.json', 'utf8')) as {
    files: Array<{ path: string; encodedSha256: string; decodedRgba8Sha256: string }>;
  };
  assert.equal(smoke.files.length, 16);
  for (const fixture of smoke.files) {
    const bytes = await readFile(`reference-set/${fixture.path}`);
    const decoded = decodeReferencePng(bytes);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), fixture.encodedSha256, fixture.path);
    assert.equal(createHash('sha256').update(decoded.rgba).digest('hex'), fixture.decodedRgba8Sha256, fixture.path);
  }
});
