import { createHash } from 'node:crypto';
import { inflateSync } from 'node:zlib';
import { readdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { getResultAliasMapping } from '../legacy-ts/src/legacy.js';

const MANIFEST_FORMAT = 'binblock-reference-manifest/v1';
const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

export type EquivalenceClass = 'pixel-exact' | 'alpha-only-exact' | 'bounded-difference' | 'raster-fallback' | 'pixel-alias' | 'byte-alias';

export interface ReferenceManifestAlias {
  identity: 'pixels' | 'bytes';
  target: string;
}

export interface ReferenceManifestFile {
  path: string;
  family: string;
  byteLength: number;
  encodedSha256: string;
  width: number;
  height: number;
  pixelFormat: 'RGB8_UNORM' | 'RGBA8_UNORM';
  alphaPresence: 'none' | 'opaque' | 'translucent';
  decodedRgba8Sha256: string;
  equivalence: EquivalenceClass;
  alias?: ReferenceManifestAlias;
}

export interface ReferenceManifestFamily {
  id: string;
  path: string;
  imageCount: number;
}

export interface ReferenceManifest {
  format: typeof MANIFEST_FORMAT;
  root: 'reference-set';
  fileCount: number;
  families: ReferenceManifestFamily[];
  files: ReferenceManifestFile[];
}

interface FamilyDefinition {
  id: string;
  path: string;
  imageCount: number;
  defaultEquivalence: EquivalenceClass;
}

interface DecodedPng {
  width: number;
  height: number;
  pixelFormat: 'RGB8_UNORM' | 'RGBA8_UNORM';
  alphaPresence: 'none' | 'opaque' | 'translucent';
  rgba: Uint8Array;
}

const FAMILY_DEFINITIONS: readonly FamilyDefinition[] = [
  { id: 'flat-color', path: 'col', imageCount: 16, defaultEquivalence: 'pixel-exact' },
  { id: 'gradient-masks', path: 'Gradient Layers Alpha Maps', imageCount: 19, defaultEquivalence: 'alpha-only-exact' },
  { id: 'gradient-variants', path: 'col bin 2', imageCount: 1202, defaultEquivalence: 'raster-fallback' },
  { id: 'downscaled', path: 'blue 64-8 24 bit', imageCount: 88, defaultEquivalence: 'pixel-exact' },
  { id: 'foreground-alpha', path: 'red FG-Alpha', imageCount: 22, defaultEquivalence: 'raster-fallback' },
  {
    id: 'foreground-composites',
    path: 'Red-col fg-alpha Print',
    imageCount: 815,
    defaultEquivalence: 'raster-fallback',
  },
  { id: 'elliptical-gradients', path: 'brown bear', imageCount: 65, defaultEquivalence: 'raster-fallback' },
  { id: 'layer-compositions-curated', path: 'out4 - Select Library', imageCount: 468, defaultEquivalence: 'raster-fallback' },
  { id: 'layer-compositions-modding', path: 'out4 - modding', imageCount: 208, defaultEquivalence: 'raster-fallback' },
  { id: 'layer-compositions-special', path: 'out4-special', imageCount: 385, defaultEquivalence: 'raster-fallback' },
  { id: 'sans-glyphs', path: 'No AA 64px Black+White', imageCount: 26, defaultEquivalence: 'raster-fallback' },
  { id: 'serif-glyphs', path: 'New folder', imageCount: 26, defaultEquivalence: 'raster-fallback' },
  { id: 'ordered-results', path: 'result', imageCount: 972, defaultEquivalence: 'raster-fallback' },
];

const BOUNDED_BLUE_VARIANTS = new Set([
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
]);

const BOUNDED_DOWNSCALED_VARIANTS = new Set([
  'grad00blk100',
  'grad00blk25',
  'grad00wht',
  'grad01blk100',
  'grad01wht',
  'grad02blk100',
  'grad02blk50',
  'grad02wht',
  'grad03blk100',
  'grad03blk25',
  'grad03wht100',
  'grad04blk100',
  'grad04wht',
  'grad05blk100',
  'grad05blk50',
  'grad05wht50',
  'grad06blk40',
  'grad10wht100-rotCCW',
  'grad10wht100-rotCW',
  'grad10wht100',
  'grad14blk25',
  'grad14blk50',
  'grad14wht',
  'grad15blk100',
  'grad15blk25',
  'grad15blk50',
  'grad15wht',
  'grad16blk20',
  'grad16wht',
  'grad17blk',
  'grad17blk50',
  'grad17wht',
]);

function compareText(left: string, right: string): number {
  return left < right ? -1 : left > right ? 1 : 0;
}

function sha256(bytes: Uint8Array): string {
  return createHash('sha256').update(bytes).digest('hex');
}

function checkedProduct(...values: number[]): number {
  let result = 1;
  for (const value of values) {
    if (!Number.isSafeInteger(value) || value < 0 || result > Number.MAX_SAFE_INTEGER / value) {
      throw new Error('PNG dimensions exceed the safe integer range.');
    }
    result *= value;
  }
  return result;
}

function paeth(left: number, above: number, upperLeft: number): number {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) return left;
  return aboveDistance <= upperLeftDistance ? above : upperLeft;
}

export function decodeReferencePng(bytes: Uint8Array): DecodedPng {
  const png = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (png.length < PNG_SIGNATURE.length || !png.subarray(0, PNG_SIGNATURE.length).equals(PNG_SIGNATURE)) {
    throw new Error('File does not have a valid PNG signature.');
  }

  let width = 0;
  let height = 0;
  let bytesPerPixel = 0;
  let pixelFormat: DecodedPng['pixelFormat'] | undefined;
  const compressed: Buffer[] = [];
  let offset = PNG_SIGNATURE.length;
  while (offset + 12 <= png.length) {
    const length = png.readUInt32BE(offset);
    const type = png.toString('ascii', offset + 4, offset + 8);
    const dataStart = offset + 8;
    const dataEnd = dataStart + length;
    if (dataEnd + 4 > png.length) throw new Error(`PNG chunk ${type} exceeds the file length.`);
    const data = png.subarray(dataStart, dataEnd);
    if (type === 'IHDR') {
      if (length !== 13) throw new Error('PNG IHDR chunk has an invalid length.');
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      const bitDepth = data[8];
      const colorType = data[9];
      const compression = data[10];
      const filter = data[11];
      const interlace = data[12];
      if (width === 0 || height === 0) throw new Error('PNG dimensions must be non-zero.');
      if (bitDepth !== 8 || (colorType !== 2 && colorType !== 6) || compression !== 0 || filter !== 0 || interlace !== 0) {
        throw new Error('Reference PNGs must be non-interlaced 8-bit RGB or RGBA images.');
      }
      bytesPerPixel = colorType === 2 ? 3 : 4;
      pixelFormat = colorType === 2 ? 'RGB8_UNORM' : 'RGBA8_UNORM';
    } else if (type === 'IDAT') {
      compressed.push(data);
    } else if (type === 'IEND') {
      offset = dataEnd + 4;
      break;
    }
    offset = dataEnd + 4;
  }
  if (!pixelFormat || width === 0 || height === 0) throw new Error('PNG is missing its IHDR chunk.');
  if (compressed.length === 0) throw new Error('PNG is missing image data.');

  const rowLength = checkedProduct(width, bytesPerPixel);
  const expectedInflatedLength = checkedProduct(height, rowLength + 1);
  const inflated = inflateSync(Buffer.concat(compressed));
  if (inflated.length !== expectedInflatedLength) {
    throw new Error(`PNG image data has length ${inflated.length}; expected ${expectedInflatedLength}.`);
  }

  const reconstructed = new Uint8Array(checkedProduct(width, height, bytesPerPixel));
  let sourceOffset = 0;
  for (let y = 0; y < height; y += 1) {
    const filterType = inflated[sourceOffset++];
    if (filterType > 4) throw new Error(`PNG scanline ${y} uses unknown filter ${filterType}.`);
    const rowOffset = y * rowLength;
    const previousRowOffset = rowOffset - rowLength;
    for (let x = 0; x < rowLength; x += 1) {
      const filtered = inflated[sourceOffset++];
      const left = x >= bytesPerPixel ? reconstructed[rowOffset + x - bytesPerPixel] : 0;
      const above = y > 0 ? reconstructed[previousRowOffset + x] : 0;
      const upperLeft = y > 0 && x >= bytesPerPixel ? reconstructed[previousRowOffset + x - bytesPerPixel] : 0;
      let predictor = 0;
      if (filterType === 1) predictor = left;
      else if (filterType === 2) predictor = above;
      else if (filterType === 3) predictor = Math.floor((left + above) / 2);
      else if (filterType === 4) predictor = paeth(left, above, upperLeft);
      reconstructed[rowOffset + x] = (filtered + predictor) & 0xff;
    }
  }

  const rgba = new Uint8Array(checkedProduct(width, height, 4));
  let hasTransparency = false;
  for (let source = 0, destination = 0; source < reconstructed.length; source += bytesPerPixel, destination += 4) {
    rgba[destination] = reconstructed[source];
    rgba[destination + 1] = reconstructed[source + 1];
    rgba[destination + 2] = reconstructed[source + 2];
    const alpha = bytesPerPixel === 4 ? reconstructed[source + 3] : 255;
    rgba[destination + 3] = alpha;
    hasTransparency ||= alpha !== 255;
  }

  return {
    width,
    height,
    pixelFormat,
    alphaPresence: pixelFormat === 'RGB8_UNORM' ? 'none' : hasTransparency ? 'translucent' : 'opaque',
    rgba,
  };
}

async function walkPngs(directory: string): Promise<string[]> {
  const entries = await readdir(directory, { withFileTypes: true });
  const paths: string[] = [];
  for (const entry of entries.sort((left, right) => compareText(left.name, right.name))) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) paths.push(...(await walkPngs(path)));
    else if (entry.isFile() && entry.name.toLowerCase().endsWith('.png')) paths.push(path);
  }
  return paths;
}

function classifyFamily(path: string): FamilyDefinition {
  const topLevel = path.split('/')[0];
  const family = FAMILY_DEFINITIONS.find((candidate) => candidate.path === topLevel);
  if (!family) throw new Error(`No reference family is registered for ${path}.`);
  return family;
}

function knownEquivalence(path: string, family: FamilyDefinition): EquivalenceClass {
  if (path === 'Gradient Layers Alpha Maps/18.png') return 'raster-fallback';
  if (path.startsWith('col bin 2/rgb/col_blue_hi-')) {
    const variant = path.slice('col bin 2/rgb/col_blue_hi-'.length, -'.png'.length);
    if (BOUNDED_BLUE_VARIANTS.has(variant)) return 'bounded-difference';
  }
  if (path.startsWith('blue 64-8 24 bit/col_blue_hi-')) {
    const variant = path.slice('blue 64-8 24 bit/col_blue_hi-'.length, -'.png'.length);
    if (variant === 'grad06blk50') return 'raster-fallback';
    if (BOUNDED_DOWNSCALED_VARIANTS.has(variant)) return 'bounded-difference';
  }
  return family.defaultEquivalence;
}

function canonicalAliases(files: readonly ReferenceManifestFile[], field: 'encodedSha256' | 'decodedRgba8Sha256'): Map<string, string> {
  const canonicalByHash = new Map<string, string>();
  const aliases = new Map<string, string>();
  for (const file of files) {
    const hash = file[field];
    const canonical = canonicalByHash.get(hash);
    if (canonical) aliases.set(file.path, canonical);
    else canonicalByHash.set(hash, file.path);
  }
  return aliases;
}

function applyAliases(files: ReferenceManifestFile[]): void {
  const byteAliases = canonicalAliases(files, 'encodedSha256');
  const pixelAliases = canonicalAliases(files, 'decodedRgba8Sha256');
  const filesByPath = new Map(files.map((file) => [file.path, file]));
  for (const file of files) {
    if (file.family === 'ordered-results') {
      const match = /^result\/ColBinSet_(\d{4})\.png$/.exec(file.path);
      if (!match) throw new Error(`Ordered result has an unexpected path: ${file.path}.`);
      const mapping = getResultAliasMapping(Number.parseInt(match[1], 10));
      if (mapping.exactPixelAliasExpected) {
        const target = filesByPath.get(mapping.rgbPath);
        if (!target) throw new Error(`Ordered result alias target does not exist: ${mapping.rgbPath}.`);
        if (target.decodedRgba8Sha256 !== file.decodedRgba8Sha256) {
          throw new Error(`Ordered result alias pixels drifted: ${file.path} != ${mapping.rgbPath}.`);
        }
        file.equivalence = 'pixel-alias';
        file.alias = { identity: 'pixels', target: mapping.rgbPath };
      }
      continue;
    }
    const byteTarget = byteAliases.get(file.path);
    if (byteTarget) {
      file.equivalence = 'byte-alias';
      file.alias = { identity: 'bytes', target: byteTarget };
      continue;
    }
    const pixelTarget = pixelAliases.get(file.path);
    if (pixelTarget) {
      file.equivalence = 'pixel-alias';
      file.alias = { identity: 'pixels', target: pixelTarget };
    }
  }
}

export async function generateReferenceManifest(repositoryRoot = process.cwd()): Promise<ReferenceManifest> {
  const archiveRoot = join(repositoryRoot, 'reference-set');
  const absolutePaths = await walkPngs(archiveRoot);
  const files: ReferenceManifestFile[] = [];
  for (const absolutePath of absolutePaths) {
    const path = relative(archiveRoot, absolutePath).split('\\').join('/');
    const family = classifyFamily(path);
    const bytes = await readFile(absolutePath);
    const decoded = decodeReferencePng(bytes);
    files.push({
      path,
      family: family.id,
      byteLength: bytes.length,
      encodedSha256: sha256(bytes),
      width: decoded.width,
      height: decoded.height,
      pixelFormat: decoded.pixelFormat,
      alphaPresence: decoded.alphaPresence,
      decodedRgba8Sha256: sha256(decoded.rgba),
      equivalence: knownEquivalence(path, family),
    });
  }

  if (files.length !== 4312) throw new Error(`Reference archive contains ${files.length} PNGs; expected 4312.`);
  for (const family of FAMILY_DEFINITIONS) {
    const count = files.filter((file) => file.family === family.id).length;
    if (count !== family.imageCount) {
      throw new Error(`Reference family ${family.id} contains ${count} PNGs; expected ${family.imageCount}.`);
    }
  }
  applyAliases(files);
  return {
    format: MANIFEST_FORMAT,
    root: 'reference-set',
    fileCount: files.length,
    families: FAMILY_DEFINITIONS.map(({ id, path, imageCount }) => ({ id, path, imageCount })),
    files,
  };
}

export function serializeReferenceManifest(manifest: ReferenceManifest): string {
  return `${JSON.stringify(manifest, null, 2)}\n`;
}

async function main(): Promise<void> {
  const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
  const outputPath = join(repositoryRoot, 'reference-manifest.json');
  const serialized = serializeReferenceManifest(await generateReferenceManifest(repositoryRoot));
  if (process.argv.includes('--check')) {
    const current = await readFile(outputPath, 'utf8').catch(() => '');
    if (current !== serialized) throw new Error('reference-manifest.json is stale; run npm run inventory.');
    console.log('reference-manifest.json matches all 4,312 reference PNGs.');
    return;
  }
  await writeFile(outputPath, serialized);
  console.log(`Wrote deterministic inventory for 4,312 PNGs to ${outputPath}.`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await main();
}
