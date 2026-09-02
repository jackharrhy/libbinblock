import type { BinBlockImage } from '../bindings/javascript/binblock.js';

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
  const compressed = new Uint8Array(idat.reduce((total, chunk) => total + chunk.length, 0));
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
