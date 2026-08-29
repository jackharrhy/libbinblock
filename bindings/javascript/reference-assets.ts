import type { BinBlockImage, BinBlockRuntime } from './binblock.js';

interface ReferenceManifestFile {
  path: string;
  encodedSha256: string;
  width: number;
  height: number;
}

interface ReferenceManifest {
  format: string;
  fileCount: number;
  files: readonly ReferenceManifestFile[];
}

function readU32(bytes: Uint8Array, offset: number): number {
  return new DataView(bytes.buffer, bytes.byteOffset + offset, 4).getUint32(0, false);
}

function paeth(left: number, above: number, upperLeft: number): number {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  return leftDistance <= aboveDistance && leftDistance <= upperLeftDistance ? left : aboveDistance <= upperLeftDistance ? above : upperLeft;
}

async function inflate(bytes: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([Uint8Array.from(bytes)]).stream().pipeThrough(new DecompressionStream('deflate'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

export async function decodeReferencePng(encoded: Uint8Array): Promise<BinBlockImage> {
  const signature = [137, 80, 78, 71, 13, 10, 26, 10];
  if (encoded.length < signature.length || signature.some((value, index) => encoded[index] !== value))
    throw new Error('Reference asset is not a PNG.');
  let width = 0;
  let height = 0;
  let colorType = 0;
  const idat: Uint8Array[] = [];
  let offset = 8;
  while (offset + 12 <= encoded.length) {
    const length = readU32(encoded, offset);
    const type = new TextDecoder().decode(encoded.subarray(offset + 4, offset + 8));
    const start = offset + 8;
    const end = start + length;
    if (end + 4 > encoded.length) throw new Error('Reference PNG chunk exceeds its byte stream.');
    if (type === 'IHDR') {
      if (length !== 13) throw new Error('Reference PNG has an invalid IHDR.');
      width = readU32(encoded, start);
      height = readU32(encoded, start + 4);
      colorType = encoded[start + 9];
      if (
        width === 0 ||
        height === 0 ||
        encoded[start + 8] !== 8 ||
        (colorType !== 2 && colorType !== 6) ||
        encoded[start + 10] !== 0 ||
        encoded[start + 11] !== 0 ||
        encoded[start + 12] !== 0
      )
        throw new Error('Reference PNG uses an unsupported pixel format.');
    } else if (type === 'IDAT') idat.push(encoded.slice(start, end));
    else if (type === 'IEND') break;
    offset = end + 4;
  }
  if (width === 0 || height === 0 || idat.length === 0) throw new Error('Reference PNG is incomplete.');
  const compressedLength = idat.reduce((total, chunk) => total + chunk.length, 0);
  const compressed = new Uint8Array(compressedLength);
  let compressedOffset = 0;
  for (const chunk of idat) {
    compressed.set(chunk, compressedOffset);
    compressedOffset += chunk.length;
  }
  const channels = colorType === 2 ? 3 : 4;
  const stride = width * channels;
  const filtered = await inflate(compressed);
  if (filtered.length !== height * (stride + 1)) throw new Error('Reference PNG scanline length is invalid.');
  const raw = new Uint8Array(width * height * channels);
  for (let y = 0; y < height; y += 1) {
    const filter = filtered[y * (stride + 1)];
    for (let x = 0; x < stride; x += 1) {
      const source = filtered[y * (stride + 1) + x + 1];
      const destination = y * stride + x;
      const left = x >= channels ? raw[destination - channels] : 0;
      const above = y > 0 ? raw[destination - stride] : 0;
      const upperLeft = y > 0 && x >= channels ? raw[destination - stride - channels] : 0;
      if (filter === 0) raw[destination] = source;
      else if (filter === 1) raw[destination] = source + left;
      else if (filter === 2) raw[destination] = source + above;
      else if (filter === 3) raw[destination] = source + Math.floor((left + above) / 2);
      else if (filter === 4) raw[destination] = source + paeth(left, above, upperLeft);
      else throw new Error('Reference PNG uses an unsupported filter.');
    }
  }
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let source = 0, destination = 0; source < raw.length; source += channels, destination += 4) {
    pixels[destination] = raw[source];
    pixels[destination + 1] = raw[source + 1];
    pixels[destination + 2] = raw[source + 2];
    pixels[destination + 3] = channels === 4 ? raw[source + 3] : 255;
  }
  return { width, height, pixels };
}

async function sha256(bytes: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', Uint8Array.from(bytes));
  return [...new Uint8Array(digest)].map((value) => value.toString(16).padStart(2, '0')).join('');
}

function assetUrl(root: URL, path: string): URL {
  return new URL(`reference-set/${path.split('/').map(encodeURIComponent).join('/')}`, root);
}

export class ReferenceAssetHost {
  readonly source: string;
  readonly #runtime: BinBlockRuntime;
  readonly #root: URL;
  readonly #fileByContentId = new Map<string, ReferenceManifestFile>();
  readonly #pending = new Map<string, Promise<void>>();

  private constructor(runtime: BinBlockRuntime, root: URL, source: string, manifest: ReferenceManifest) {
    this.#runtime = runtime;
    this.#root = root;
    this.source = source;
    for (const file of manifest.files) {
      runtime.registerAssetMetadata({
        logicalId: file.path,
        contentId: file.encodedSha256,
        width: file.width,
        height: file.height,
        hasEncodedBytes: true,
      });
      if (!this.#fileByContentId.has(file.encodedSha256)) this.#fileByContentId.set(file.encodedSha256, file);
    }
  }

  static async load(runtime: BinBlockRuntime, root = new URL('../', document.baseURI)): Promise<ReferenceAssetHost> {
    const [manifestResponse, sourceResponse] = await Promise.all([
      fetch(new URL('reference-manifest.json', root)),
      fetch(new URL('reference-set/reference-set.binscript', root)),
    ]);
    if (!manifestResponse.ok || !sourceResponse.ok) throw new Error('The reference program metadata could not be loaded.');
    const manifest = (await manifestResponse.json()) as ReferenceManifest;
    if (manifest.format !== 'binblock-reference-manifest/v1' || manifest.fileCount !== 4312)
      throw new Error('The reference manifest version or file count is invalid.');
    return new ReferenceAssetHost(runtime, root, await sourceResponse.text(), manifest);
  }

  async hydrateGraph(root: number): Promise<void> {
    await Promise.all(this.#runtime.requiredAssetContentIds(root).map((contentId) => this.#hydrate(contentId)));
  }

  async #hydrate(contentId: string): Promise<void> {
    if (this.#runtime.assetIsHydrated(contentId)) return;
    const existing = this.#pending.get(contentId);
    if (existing) return existing;
    const pending = (async () => {
      const file = this.#fileByContentId.get(contentId);
      if (!file) throw new Error(`No reference metadata exists for content ${contentId}.`);
      const response = await fetch(assetUrl(this.#root, file.path));
      if (!response.ok) throw new Error(`Reference asset could not be fetched: ${file.path}.`);
      const encoded = new Uint8Array(await response.arrayBuffer());
      if ((await sha256(encoded)) !== contentId) throw new Error(`Reference asset hash mismatch: ${file.path}.`);
      const image = await decodeReferencePng(encoded);
      if (image.width !== file.width || image.height !== file.height) throw new Error(`Reference asset dimensions drifted: ${file.path}.`);
      this.#runtime.hydrateAsset(contentId, image, encoded);
    })();
    this.#pending.set(contentId, pending);
    try {
      await pending;
    } finally {
      this.#pending.delete(contentId);
    }
  }
}
