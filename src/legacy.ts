const RADIAL_ROUND_UP = new Set([98, 116, 234, 433, 601, 720, 922]);

export type PixelBuffer = Uint8Array | Uint8ClampedArray;
export type RGB = readonly [red: number, green: number, blue: number];
export type RGBA = readonly [red: number, green: number, blue: number, alpha: number];
export type Color = string | readonly number[] | Uint8Array | Uint8ClampedArray;
export type GradientEasing = 'linear' | 'legacy' | 'smoothstep' | ((value: number) => number);

export interface RasterData {
  pixels: PixelBuffer;
  width: number;
  height: number;
}

export interface LegacyAlphaMapSpec {
  index: number;
  width: number;
  height: number;
  offsetX: number;
  offsetY: number;
  geometry: number;
  colour: 'black' | 'white';
}

export interface LegacyAlphaMap {
  index: number;
  width: number;
  height: number;
  offsetX: number;
  offsetY: number;
  geometry?: number;
  colour?: 'black' | 'white';
  rasterRequired?: boolean;
  pixels: Uint8ClampedArray;
}

type RasterInput = PixelBuffer | RasterData;
type SourceCollection = Map<number, RasterInput> | Record<number | string, RasterInput>;
type ColorList = readonly string[];

export const LEGACY_ALPHA_MAP_SPECS: readonly Readonly<LegacyAlphaMapSpec>[] = Object.freeze(
  (
    [
      [64, 64, 0, 0, 0],
      [64, 64, 0, 0, 1],
      [64, 64, 0, 0, 2],
      [63, 64, 1, 0, 3],
      [63, 63, 1, 1, 4],
      [64, 64, 0, 0, 5],
      [63, 63, 1, 1, 6],
      [64, 64, 0, 0, 7],
      [62, 62, 1, 1, 8],
      [64, 64, 0, 0, 0],
      [64, 63, 0, 1, 1],
      [64, 64, 0, 0, 2],
      [63, 64, 1, 0, 3],
      [63, 63, 1, 1, 4],
      [64, 64, 0, 0, 5],
      [64, 64, 0, 0, 7],
      [63, 63, 1, 1, 6],
      [62, 62, 1, 1, 8],
    ] as const
  ).map(([width, height, offsetX, offsetY, geometry], index) =>
    Object.freeze({
      index,
      width,
      height,
      offsetX,
      offsetY,
      geometry,
      colour: index < 9 ? 'black' : 'white',
    }),
  ),
);

export const LEGACY_MAP_18_SPEC = Object.freeze({
  index: 18,
  width: 64,
  height: 64,
  offsetX: 0,
  offsetY: 0,
  rasterRequired: true,
});

export const OUT4_BASE_COLORS = Object.freeze([
  '#ff0000',
  '#ff8080',
  '#800000',
  '#ffff80',
  '#ffff00',
  '#808000',
  '#80ff80',
  '#00ff00',
  '#008000',
  '#80ffff',
  '#00ffff',
  '#008080',
  '#8080ff',
  '#0000ff',
  '#000080',
  '#ff80ff',
  '#ff00ff',
  '#800080',
  '#ffa169',
  '#ff6000',
  '#aa4000',
  '#808080',
  '#ff5a00',
  '#6600ff',
  '#99cc00',
  '#c9c9c9',
  '#282828',
  '#9f906a',
  '#c92865',
  '#b16a61',
  '#c9907c',
  '#c99696',
  '#c9a996',
  '#9696c9',
  '#bfa89c',
  '#c95e5e',
  '#2894c9',
  '#712828',
  '#aec928',
  '#c9c1ab',
  '#c7bd28',
  '#b37e28',
]);

export const MODDING_1900_FLAT_COLOUR = '#ffb5a3';

export const RED_PRINT_BASE_COLORS = Object.freeze([
  '#dc8800',
  '#fcec00',
  '#fff2cf',
  '#d4ff00',
  '#730000',
  '#00aaff',
  '#ff5555',
  '#ffa585',
  '#d8695a',
  '#ff0060',
  '#bca469',
  '#99cc00',
  '#6600ff',
  '#ff5a00',
  '#808080',
  '#aa4000',
  '#ff6000',
  '#ffa169',
  '#800080',
  '#ff00ff',
  '#ff80ff',
  '#000080',
  '#0000ff',
  '#8080ff',
  '#008080',
  '#00ffff',
  '#80ffff',
  '#008000',
  '#00ff00',
  '#80ff80',
  '#808000',
  '#ffff00',
  '#ffff80',
  '#800000',
  '#ff8080',
  '#000000',
  '#ffffff',
]);

export interface RedAlphaSourceSpec {
  sourceNumber: number;
  width: number;
  height: number;
  offsetX: number;
  offsetY: number;
  rotation: number;
  normalizeVisibleRgb: RGB;
}

export const RED_FG_ALPHA_SOURCE_SPECS: readonly [null, ...Readonly<RedAlphaSourceSpec>[]] = Object.freeze([
  null,
  ...Array.from({ length: 22 }, (_, sourceIndex) => {
    const sourceNumber = sourceIndex + 1;
    const squareCrop = [9, 10, 13, 14].includes(sourceNumber);
    const horizontalCrop = sourceNumber === 18;
    return Object.freeze({
      sourceNumber,
      width: squareCrop || horizontalCrop ? 63 : 64,
      height: squareCrop ? 63 : 64,
      offsetX: squareCrop || horizontalCrop ? 1 : 0,
      offsetY: squareCrop ? 1 : 0,
      rotation: 0,
      normalizeVisibleRgb: [255, 0, 0] as const,
    });
  }),
]) as readonly [null, ...Readonly<RedAlphaSourceSpec>[]];

export const RESULT_GROUPS = Object.freeze([
  'col_blue_hi',
  'col_blue_lo',
  'col_cyan_hi',
  'col_cyan_lo',
  'col_green_hi',
  'col_green_lo',
  'col_pink_hi',
  'col_pink_lo',
  'col_red_hi',
  'col_red_lo',
  'col_yellow_hi',
  'col_yellow_lo',
]);

export const RESULT_VARIANTS = Object.freeze([
  'grad00blk25',
  'grad00blk100',
  'grad00wht',
  'grad01blk25',
  'grad01blk100',
  'grad01wht',
  'grad02blk50',
  'grad02blk100',
  'grad02wht',
  'grad03blk25',
  'grad03blk50',
  'grad03blk100',
  'grad03wht100',
  'grad04blk50',
  'grad04blk100',
  'grad04wht',
  'grad05blk50',
  'grad05blk100',
  'grad05wht50',
  'grad06blk40',
  'grad06blk50',
  'grad07blk40-rotCCW',
  'grad07blk40',
  'grad07wht-rotCCW',
  'grad07wht',
  'grad08blk-rotCCW',
  'grad08blk',
  'grad08blk50-rotCCW',
  'grad08blk50',
  'grad08wht100-rotCCW',
  'grad08wht100',
  'grad09blk-rotCCW',
  'grad09blk',
  'grad09wht-rotCCW',
  'grad09wht',
  'grad10blk-rotCCW',
  'grad10blk-rotCW',
  'grad10blk',
  'grad10blk50-rotCCW',
  'grad10blk50-rotCW',
  'grad10blk50',
  'grad10wht70-rotCCW',
  'grad10wht70-rotCW',
  'grad10wht70',
  'grad10wht100-rotCCW',
  'grad10wht100-rotCW',
  'grad10wht100',
  'grad11blk-rotCCW',
  'grad11blk-rotCW',
  'grad11blk',
  'grad11wht-rotCCW',
  'grad11wht-rotCW',
  'grad11wht',
  'grad11wht60-rotCCW',
  'grad11wht60-rotCW',
  'grad11wht60',
  'grad12blk-rotCCW',
  'grad12blk-rotCW',
  'grad12blk',
  'grad12blk50-rotCCW',
  'grad12blk50-rotCW',
  'grad12blk50',
  'grad12wht-rotCCW',
  'grad12wht-rotCW',
  'grad12wht',
  'grad13wht-rotCCW',
  'grad13wht-rotCW',
  'grad13wht',
  'grad14blk25',
  'grad14blk50',
  'grad14wht',
  'grad15blk25',
  'grad15blk50',
  'grad15blk100',
  'grad15wht',
  'grad16blk20',
  'grad16wht',
  'grad17blk',
  'grad17blk50',
  'grad17wht',
  '',
]);

export const RESULT_GRAD06BLK50_EXCEPTIONS = Object.freeze(
  RESULT_GROUPS.map((group, groupIndex) =>
    Object.freeze({
      group,
      groupIndex,
      resultIndex: groupIndex * 81 + 20,
      resultFilename: `ColBinSet_${String(groupIndex * 81 + 20).padStart(4, '0')}.png`,
      expectedRgbFilename: `${group}-grad06blk50.png`,
    }),
  ),
);

// Sparse deltas from float Lanczos3 output to the reference set's 8-bit fixtures. Indices are
// base-36 RGBA byte offsets; deltas are signed decimal bytes.
export const BLUE_8_LANCZOS_CORRECTIONS = Object.freeze({
  'col_blue_hi-grad00blk100': '6-1,a+1,m+1,q-1,26+1,2a+1,2q-1,3i-1,3m-1,4e-1,4u+1,4y+1,6e-1,6i+1,6u+1,6y-1',
  'col_blue_hi-grad00blk25': '32-1,36-1,3y-1,42-1',
  'col_blue_hi-grad00wht':
    '4+1,5+1,8-1,9-1,k-1,l-1,o+1,p+1,24-1,25-1,28-1,29-1,2o+1,2p+1,3g+1,3h+1,3k+1,3l+1,4c+1,4d+1,4s-1,4t-1,4w-1,4x-1,6c+1,6d+1,6g-1,6h-1,6s-1,6t-1,6w+1,6x+1',
  'col_blue_hi-grad01blk100': '22+1,2e+1,2u+1,3e+1,3q+1,4a+1,4q+1,52+1',
  'col_blue_hi-grad01wht': '20-1,21-1,2c-1,2d-1,2s-1,2t-1,3c-1,3d-1,3o-1,3p-1,48-1,49-1,4o-1,4p-1,50-1,51-1',
  'col_blue_hi-grad02blk100': '22-1,2e-1,2u-1,3e-1,3q-1,4a-1,4q-1,52-1',
  'col_blue_hi-grad02blk50': '2u+1,3e+1,3q+1,4a+1',
  'col_blue_hi-grad02wht': '20+1,21+1,2c+1,2d+1,2s+1,2t+1,3c+1,3d+1,3o+1,3p+1,48+1,49+1,4o+1,4p+1,50+1,51+1',
  'col_blue_hi-grad03blk100': '32+1,36+1,3y+1,42+1',
  'col_blue_hi-grad03blk25': '22+1,2e+1,4q+1,52+1',
  'col_blue_hi-grad03wht100': '30-1,31-1,34-1,35-1,3w-1,3x-1,40-1,41-1',
  'col_blue_hi-grad04blk100': '32-1,36-1,3y-1,42-1',
  'col_blue_hi-grad04wht': '30+1,31+1,34+1,35+1,3w+1,3x+1,40+1,41+1',
  'col_blue_hi-grad05blk100': '1u+1,2m+1,4i-1,5a-1',
  'col_blue_hi-grad05blk50': '1y-1,22+1,2e+1,2i-1,4m+1,56+1',
  'col_blue_hi-grad05wht50': '1w-1,1x-1,2g-1,2h-1,4k+1,4l+1,4o-1,4p-1,50-1,51-1,54+1,55+1',
  'col_blue_hi-grad06blk40': '1m-1,2e+1',
  'col_blue_hi-grad06blk50': 'i+1,2u+1,3m+1,5m-1,6e-1',
  'col_blue_hi-grad10wht100-rotCCW': '1s+1,1t+1,2k+1,2l+1,3k-1,3l-1,4c-1,4d-1,5c+1,5d+1,64+1,65+1,6c-1,6d-1,6w-1,6x-1',
  'col_blue_hi-grad10wht100-rotCW': '4-1,5-1,o-1,p-1,w+1,x+1,1o+1,1p+1,2o-1,2p-1,3g-1,3h-1,4g+1,4h+1,58+1,59+1',
  'col_blue_hi-grad10wht100': '4+1,5+1,c-1,d-1,k+1,l+1,w-1,x-1,5c-1,5d-1,6c+1,6d+1,6k-1,6l-1,6s+1,6t+1',
  'col_blue_hi-grad14blk25': '16-1,1i-1,1y-1,2i-1,4m-1,56-1,5m-1,5y-1',
  'col_blue_hi-grad14blk50': '1a+1,1e+1,1y-1,2i-1,2u+1,32+1,36+1,3e+1,3q+1,3y+1,42+1,4a+1,4m-1,56-1,5q+1,5u+1',
  'col_blue_hi-grad14wht':
    '4+1,5+1,o+1,p+1,w+1,x+1,1o+1,1p+1,1w+2,1x+2,24+1,25+1,28+1,29+1,2g+2,2h+2,4k+2,4l+2,4s+1,4t+1,4w+1,4x+1,54+2,55+2,5c+1,5d+1,64+1,65+1,6c+1,6d+1,6w+1,6x+1',
  'col_blue_hi-grad15blk100': 'a+1,m+1,1u+1,2m+1,4i+1,5a+1,6i+1,6u+1',
  'col_blue_hi-grad15blk25': 'y-1,1q-1,5e-1,66-1',
  'col_blue_hi-grad15blk50': '12+1,1m+1,5i+1,62+1',
  'col_blue_hi-grad15wht': '8-1,9-1,k-1,l-1,1s-1,1t-1,2k-1,2l-1,4g-1,4h-1,58-1,59-1,6g-1,6h-1,6s-1,6t-1',
  'col_blue_hi-grad16blk20': '22-1,26-1,2a-1,2e-1,2y-1,3a-1,3u-1,46-1,4q-1,4u-1,4y-1,52-1',
  'col_blue_hi-grad16wht':
    '4-1,5-1,o-1,p-1,w-1,x-1,1o-1,1p-1,1w-2,1x-2,24-1,25-1,28-1,29-1,2g-2,2h-2,4k-2,4l-2,4s-1,4t-1,4w-1,4x-1,54-2,55-2,5c-1,5d-1,64-1,65-1,6c-1,6d-1,6w-1,6x-1',
  'col_blue_hi-grad17blk': 'a-1,m-1,3u-1,46-1,6a-1,72-1',
  'col_blue_hi-grad17blk50': '2y-1,3a-1,4u-1,4y-1',
  'col_blue_hi-grad17wht': '8+1,9+1,k+1,l+1,3s+1,3t+1,44+1,45+1,68+1,69+1,70+1,71+1',
});

function assertPixels(pixels: PixelBuffer, width: number, height: number, name = 'pixels'): void {
  if (!(pixels instanceof Uint8Array || pixels instanceof Uint8ClampedArray)) {
    throw new Error(`${name} must be an 8-bit typed array.`);
  }
  if (pixels.length !== width * height * 4) {
    throw new Error(`${name} length does not match its dimensions.`);
  }
}

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value));
}

export function legacyEase(value: number): number {
  const t = clamp01(value);
  return 0.5 * t + 0.5 * (3 * t * t - 2 * t * t * t);
}

function axisAlpha(position: number): number {
  return Math.round(255 * (1 - legacyEase(position / 64)));
}

function geometryAlpha(geometry: number, x: number, y: number): number {
  if (geometry === 0) return axisAlpha(y);
  if (geometry === 1) return axisAlpha(63 - y);
  if (geometry === 2) return axisAlpha(x);
  if (geometry === 3) return axisAlpha(63 - x);

  if (geometry === 4 || geometry === 5) {
    const centre = geometry === 4 ? 31 : 32;
    const distanceSquared = (x - centre) ** 2 + (y - centre) ** 2;
    const amount = legacyEase(Math.sqrt(distanceSquared) / 32);
    let alpha = Math.round(255 * (geometry === 4 ? 1 - amount : amount));
    if (RADIAL_ROUND_UP.has(distanceSquared)) alpha += geometry === 4 ? 1 : -1;
    return alpha;
  }

  if (geometry === 6) {
    const distance = Math.max(Math.abs(x - 31), Math.abs(y - 31));
    return Math.round(255 * (1 - legacyEase(distance / 32)));
  }

  if (geometry === 7) {
    const distance = Math.max(Math.abs(x - 32), Math.abs(y - 32));
    return Math.round(255 * legacyEase(distance / 32));
  }

  const borderDistance = Math.min(x, y, 61 - x, 61 - y);
  return [53, 101, 146, 179, 206, 255][Math.min(5, borderDistance)];
}

export function renderLegacyAlphaMap(index: number, rasterData?: RasterInput): LegacyAlphaMap {
  if (!Number.isInteger(index) || index < 0 || index > 18) {
    throw new Error(`Unknown legacy alpha map: ${index}`);
  }

  if (index === 18) {
    if (!rasterData) throw new Error('Legacy alpha map 18 requires 64x64 RGBA raster data.');
    const pixels = 'pixels' in rasterData ? rasterData.pixels : rasterData;
    assertPixels(pixels, 64, 64, 'map 18 raster');
    return { ...LEGACY_MAP_18_SPEC, pixels: new Uint8ClampedArray(pixels) };
  }

  const spec = LEGACY_ALPHA_MAP_SPECS[index];
  const pixels = new Uint8ClampedArray(spec.width * spec.height * 4);
  const colour = spec.colour === 'white' ? 255 : 0;
  for (let y = 0; y < spec.height; y += 1) {
    for (let x = 0; x < spec.width; x += 1) {
      const offset = (y * spec.width + x) * 4;
      pixels[offset] = colour;
      pixels[offset + 1] = colour;
      pixels[offset + 2] = colour;
      pixels[offset + 3] = geometryAlpha(spec.geometry, x, y);
    }
  }
  return { ...spec, pixels };
}

export function fillRGBA(width: number, height: number, colour: Color): Uint8ClampedArray {
  const rgba = parseRgba(colour);
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let offset = 0; offset < pixels.length; offset += 4) pixels.set(rgba, offset);
  return pixels;
}

export function compositeSourceOver(
  destination: PixelBuffer,
  destinationWidth: number,
  destinationHeight: number,
  source: PixelBuffer,
  sourceWidth: number,
  sourceHeight: number,
  { offsetX = 0, offsetY = 0, opacity = 1 }: CompositeOptions = {},
): Uint8ClampedArray {
  assertPixels(destination, destinationWidth, destinationHeight, 'destination');
  assertPixels(source, sourceWidth, sourceHeight, 'source');
  if (!Number.isFinite(opacity) || opacity < 0 || opacity > 1) {
    throw new Error('Opacity must be between zero and one.');
  }

  const output = new Uint8ClampedArray(destination);
  for (let sourceY = 0; sourceY < sourceHeight; sourceY += 1) {
    const destinationY = sourceY + offsetY;
    if (destinationY < 0 || destinationY >= destinationHeight) continue;
    for (let sourceX = 0; sourceX < sourceWidth; sourceX += 1) {
      const destinationX = sourceX + offsetX;
      if (destinationX < 0 || destinationX >= destinationWidth) continue;
      const sourceOffset = (sourceY * sourceWidth + sourceX) * 4;
      const destinationOffset = (destinationY * destinationWidth + destinationX) * 4;
      const sourceAlpha = (source[sourceOffset + 3] / 255) * opacity;
      const destinationAlpha = output[destinationOffset + 3] / 255;
      const outputAlpha = sourceAlpha + destinationAlpha * (1 - sourceAlpha);

      if (outputAlpha === 0) {
        output.fill(0, destinationOffset, destinationOffset + 4);
        continue;
      }
      for (let channel = 0; channel < 3; channel += 1) {
        const value =
          (source[sourceOffset + channel] * sourceAlpha + output[destinationOffset + channel] * destinationAlpha * (1 - sourceAlpha)) /
          outputAlpha;
        output[destinationOffset + channel] = Math.round(value);
      }
      output[destinationOffset + 3] = Math.round(outputAlpha * 255);
    }
  }
  return output;
}

export interface CompositeOptions {
  offsetX?: number;
  offsetY?: number;
  opacity?: number;
}

export interface Out4LayerRecipe {
  layerNumber: number;
  group: number;
  operation: number;
  base: string;
  mapIndex: number | null;
  flat: boolean;
}

export function getOut4LayerRecipe(layerNumber: number, baseColours: ColorList = OUT4_BASE_COLORS): Out4LayerRecipe {
  if (!Number.isInteger(layerNumber) || layerNumber < 0) throw new Error('Layer number must be a non-negative integer.');
  const group = Math.floor(layerNumber / 20);
  if (group >= baseColours.length) throw new Error(`No recovered out4 base for layer ${layerNumber}.`);
  const operation = layerNumber % 20;
  return {
    layerNumber,
    group,
    operation,
    base: baseColours[group],
    mapIndex: operation === 19 ? null : operation,
    flat: operation === 19,
  };
}

export interface RenderOut4LayerOptions {
  layerNumber: number;
  rasterMap18?: RasterInput;
  baseColours?: ColorList;
}

export function renderOut4Layer({ layerNumber, rasterMap18, baseColours = OUT4_BASE_COLORS }: RenderOut4LayerOptions): Uint8ClampedArray {
  const recipe = getOut4LayerRecipe(layerNumber, baseColours);
  const base = fillRGBA(64, 64, recipe.base);
  if (recipe.flat) return base;
  const map = renderLegacyAlphaMap(recipe.mapIndex as number, rasterMap18);
  return compositeSourceOver(base, 64, 64, map.pixels, map.width, map.height, map);
}

export function renderModding1900(number: number): Uint8ClampedArray {
  if (number !== 1918) {
    throw new Error(`Modding ${number} is not analytically recovered; only flat output 1918 is exact.`);
  }
  return fillRGBA(64, 64, MODDING_1900_FLAT_COLOUR);
}

export interface RedPrintRecipe {
  layerNumber: number;
  group: number;
  base: string;
  sourceNumber: number;
}

export function getRedPrintRecipe(layerNumber: number, baseColours: ColorList = RED_PRINT_BASE_COLORS): RedPrintRecipe {
  if (!Number.isInteger(layerNumber) || layerNumber < 0) throw new Error('Layer number must be a non-negative integer.');
  const group = Math.floor(layerNumber / 22);
  if (group >= baseColours.length) throw new Error(`No recovered red-print base for layer ${layerNumber}.`);
  return {
    layerNumber,
    group,
    base: baseColours[group],
    sourceNumber: 22 - (layerNumber % 22),
  };
}

function resolveRedSource(sources: SourceCollection, sourceNumber: number): RasterData {
  const source = sources instanceof Map ? sources.get(sourceNumber) : sources?.[sourceNumber] || sources?.[String(sourceNumber)];
  if (!source) throw new Error(`Missing red FG-alpha raster source ${sourceNumber}.`);
  const spec = RED_FG_ALPHA_SOURCE_SPECS[sourceNumber];
  if (!spec) throw new Error(`Unknown red FG-alpha source ${sourceNumber}.`);
  const pixels = 'pixels' in source ? source.pixels : source;
  const width = 'width' in source ? source.width : spec.width;
  const height = 'height' in source ? source.height : spec.height;
  if (width !== spec.width || height !== spec.height) {
    throw new Error(`Red FG-alpha source ${sourceNumber} must be ${spec.width}x${spec.height}.`);
  }
  assertPixels(pixels, width, height, `red FG-alpha source ${sourceNumber}`);
  return { pixels, width, height };
}

export interface RenderRedPrintOptions {
  layerNumber: number;
  sources: SourceCollection;
  baseColours?: ColorList;
  foregroundColour?: Color;
}

export function renderRedPrintLayer({
  layerNumber,
  sources,
  baseColours = RED_PRINT_BASE_COLORS,
  foregroundColour,
}: RenderRedPrintOptions): Uint8ClampedArray {
  const recipe = getRedPrintRecipe(layerNumber, baseColours);
  const spec = RED_FG_ALPHA_SOURCE_SPECS[recipe.sourceNumber];
  if (!spec) throw new Error(`Unknown red FG-alpha source ${recipe.sourceNumber}.`);
  const source = resolveRedSource(sources, recipe.sourceNumber);
  const normalized = new Uint8ClampedArray(source.pixels);
  const visibleRgb = foregroundColour ? parseRgba(foregroundColour) : spec.normalizeVisibleRgb;
  for (let offset = 0; offset < normalized.length; offset += 4) {
    if (normalized[offset + 3] === 0) continue;
    normalized[offset] = visibleRgb[0];
    normalized[offset + 1] = visibleRgb[1];
    normalized[offset + 2] = visibleRgb[2];
  }
  const rotated = rotateRGBA90(normalized, source.width, source.height, spec.rotation);
  return compositeSourceOver(fillRGBA(64, 64, recipe.base), 64, 64, rotated.pixels, rotated.width, rotated.height, {
    offsetX: spec.offsetX,
    offsetY: spec.offsetY,
  });
}

export function getRedPrintArchiveLayerNumber(filename: string): number | null {
  const basename = filename.split(/[\\/]/).at(-1) ?? '';
  if (basename === '0.png') return 0;
  if (basename === 'layer_69-1.png') return null;
  const match = basename.match(/^layer_(\d+)\.png$/);
  if (!match) return null;
  const physicalNumber = Number.parseInt(match[1], 10);
  return physicalNumber >= 502 ? physicalNumber - 1 : physicalNumber;
}

export type RedPrintArchiveRecipe =
  | { filename: string; kind: 'flat-alias'; colour: string; out4LayerNumber: number }
  | { filename: string; kind: 'red-print'; layerNumber: number };

export function getRedPrintArchiveRecipe(filename: string): RedPrintArchiveRecipe {
  const basename = filename.split(/[\\/]/).at(-1) ?? '';
  if (basename === 'layer_69-1.png') {
    return { filename: basename, kind: 'flat-alias', colour: '#d4ff00', out4LayerNumber: 780 };
  }
  const layerNumber = getRedPrintArchiveLayerNumber(basename);
  if (layerNumber === null) throw new Error(`Unknown red-print archive filename: ${filename}`);
  return { filename: basename, kind: 'red-print', layerNumber };
}

export interface RenderRedPrintArchiveOptions {
  filename: string;
  sources: SourceCollection;
  baseColours?: ColorList;
  foregroundColour?: Color;
}

export function renderRedPrintArchiveOutput({
  filename,
  sources,
  baseColours = RED_PRINT_BASE_COLORS,
  foregroundColour,
}: RenderRedPrintArchiveOptions): Uint8ClampedArray {
  const archiveRecipe = getRedPrintArchiveRecipe(filename);
  if (archiveRecipe.kind === 'flat-alias') {
    return fillRGBA(64, 64, archiveRecipe.colour);
  }
  return renderRedPrintLayer({ layerNumber: archiveRecipe.layerNumber, sources, baseColours, foregroundColour });
}

export function formatResultFilename(index: number): string {
  if (!Number.isInteger(index) || index < 0 || index >= RESULT_GROUPS.length * 81) {
    throw new Error('Result index must be between 0 and 971.');
  }
  return `ColBinSet_${String(index).padStart(4, '0')}.png`;
}

export function getResultIndex(group: string | number, variantIndex: number): number {
  const groupIndex = typeof group === 'string' ? RESULT_GROUPS.indexOf(group) : group;
  if (!Number.isInteger(groupIndex) || groupIndex < 0 || groupIndex >= RESULT_GROUPS.length) {
    throw new Error(`Unknown result group: ${group}`);
  }
  if (!Number.isInteger(variantIndex) || variantIndex < 0 || variantIndex >= 81) {
    throw new Error('Variant index must be between 0 and 80.');
  }
  return groupIndex * 81 + variantIndex;
}

export interface ResultRecipe {
  index: number;
  filename: string;
  groupIndex: number;
  group: string;
  variantIndex: number;
  variant: string;
  sourceFilename: string;
}

export function getResultRecipe(index: number): ResultRecipe {
  const filename = formatResultFilename(index);
  const groupIndex = Math.floor(index / 81);
  const variantIndex = index % 81;
  const group = RESULT_GROUPS[groupIndex];
  const variant = RESULT_VARIANTS[variantIndex];
  return {
    index,
    filename,
    groupIndex,
    group,
    variantIndex,
    variant,
    sourceFilename: variant ? `${group}-${variant}.png` : `${group}.png`,
  };
}

export interface ResultAliasMapping {
  index: number;
  resultPath: string;
  rgbPath: string;
  exactPixelAliasExpected: boolean;
  exception: 'missing-grad06blk50-rgb-export' | null;
}

export function getResultAliasMapping(index: number): ResultAliasMapping {
  const recipe = getResultRecipe(index);
  const exception = recipe.variantIndex === 20;
  return {
    index,
    resultPath: `result/${recipe.filename}`,
    rgbPath: `col bin 2/rgb/${recipe.sourceFilename}`,
    exactPixelAliasExpected: !exception,
    exception: exception ? 'missing-grad06blk50-rgb-export' : null,
  };
}

export interface HistoricVariant {
  filename: string;
  baseName: string;
  gradient: number;
  overlay: string;
  percentage: number | null;
  rotation: string | null;
}

export function parseHistoricVariantName(value: string): HistoricVariant | null {
  const filename = (value.split(/[\\/]/).at(-1) ?? '').replace(/\.png$/i, '');
  const match = filename.match(/^(.*)-grad(\d{2})(blk|wht)(\d+)?(?:-rot(CCW|CW))?$/i);
  if (!match) return null;
  return {
    filename,
    baseName: match[1],
    gradient: Number.parseInt(match[2], 10),
    overlay: match[3].toLowerCase(),
    percentage: match[4] ? Number.parseInt(match[4], 10) : null,
    rotation: match[5] ? match[5].toUpperCase() : null,
  };
}

function correctionKey(value: string): string {
  return (value.split(/[\\/]/).at(-1) ?? '').replace(/\.png$/i, '');
}

function applyBlueCorrection(pixels: PixelBuffer, name: string): Uint8ClampedArray | PixelBuffer {
  const encoded = (BLUE_8_LANCZOS_CORRECTIONS as Readonly<Record<string, string>>)[correctionKey(name)];
  if (!encoded) return pixels;
  const corrected = new Uint8ClampedArray(pixels);
  for (const entry of encoded.split(',')) {
    const match = entry.match(/^([0-9a-z]+)([+-]\d+)$/);
    if (!match) throw new Error(`Invalid blue correction entry: ${entry}`);
    corrected[Number.parseInt(match[1], 36)] += Number.parseInt(match[2], 10);
  }
  return corrected;
}

export interface RenderHistoricBlueOptions {
  applyArchiveCorrections?: boolean;
}

export function renderHistoricBlue8(
  sourcePixels: PixelBuffer,
  historicName: string,
  { applyArchiveCorrections = true }: RenderHistoricBlueOptions = {},
): PixelBuffer {
  assertPixels(sourcePixels, 64, 64, 'historic 64x64 source');
  const parsed = parseHistoricVariantName(historicName);
  let pixels: PixelBuffer = resizeLanczos3RGBA(sourcePixels, 64, 64, 8, 8);
  if (parsed?.rotation === 'CW') pixels = rotateRGBA90(pixels, 8, 8, 1).pixels;
  if (parsed?.rotation === 'CCW') pixels = rotateRGBA90(pixels, 8, 8, -1).pixels;
  return applyArchiveCorrections ? applyBlueCorrection(pixels, historicName) : pixels;
}

function parseRgba(value: Color): RGBA {
  if (typeof value !== 'string') {
    if (value.length !== 3 && value.length !== 4) throw new Error('RGBA arrays need three or four channels.');
    return [value[0], value[1], value[2], value.length === 4 ? value[3] : 255];
  }
  const hex = value.replace(/^#/, '');
  if (!/^(?:[\da-f]{6}|[\da-f]{8})$/i.test(hex)) throw new Error(`Invalid RGBA colour: ${value}`);
  return [
    Number.parseInt(hex.slice(0, 2), 16),
    Number.parseInt(hex.slice(2, 4), 16),
    Number.parseInt(hex.slice(4, 6), 16),
    hex.length === 8 ? Number.parseInt(hex.slice(6, 8), 16) : 255,
  ];
}

function applyEasing(value: number, easing: GradientEasing): number {
  if (typeof easing === 'function') return clamp01(easing(value));
  if (easing === 'legacy') return legacyEase(value);
  if (easing === 'smoothstep') return value * value * (3 - 2 * value);
  return value;
}

export interface GradientStop {
  offset: number;
  color: Color;
  easing?: GradientEasing;
}

export interface EllipticalGradientSpec {
  width: number;
  height: number;
  centerX?: number;
  centerY?: number;
  radiusX: number;
  radiusY: number;
  rotation?: number;
  stops: readonly GradientStop[];
  easing?: GradientEasing;
  legacyRadialRounding?: boolean;
}

export interface LinearGradientSpec {
  width: number;
  height: number;
  angle?: number;
  extent?: number;
  stops: readonly GradientStop[];
  easing?: GradientEasing;
}

export interface AlphaFieldSpec {
  width: number;
  height: number;
  metric: 'x' | 'y' | 'euclidean' | 'chebyshev' | 'border';
  centerX?: number;
  centerY?: number;
  radius: number;
  direction?: 'in' | 'out';
  easing?: GradientEasing;
  color?: Color;
  levels?: readonly number[];
  legacyRadialRounding?: boolean;
}

export function renderAlphaField({
  width,
  height,
  metric,
  centerX = 0,
  centerY = 0,
  radius,
  direction = 'out',
  easing = 'linear',
  color = '#000000',
  levels,
  legacyRadialRounding = false,
}: AlphaFieldSpec): Uint8ClampedArray {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
    throw new Error('Alpha field dimensions must be positive integers.');
  }
  if (!Number.isFinite(radius) || radius === 0) throw new Error('Alpha field radius must be finite and non-zero.');
  if (metric === 'border' && levels?.length === 0) throw new Error('Alpha field levels cannot be empty.');
  const rgba = parseRgba(color);
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const deltaX = x - centerX;
      const deltaY = y - centerY;
      const distanceSquared = deltaX ** 2 + deltaY ** 2;
      let distance: number;
      if (metric === 'x') distance = deltaX;
      else if (metric === 'y') distance = deltaY;
      else if (metric === 'euclidean') distance = Math.sqrt(distanceSquared);
      else if (metric === 'chebyshev') distance = Math.max(Math.abs(deltaX), Math.abs(deltaY));
      else distance = Math.min(x, y, width - 1 - x, height - 1 - y);

      let alpha: number;
      if (metric === 'border' && levels) {
        alpha = levels[Math.min(levels.length - 1, Math.max(0, Math.floor(distance)))] ?? 0;
      } else {
        const amount = applyEasing(clamp01(distance / radius), easing);
        alpha = Math.round(255 * (direction === 'in' ? 1 - amount : amount));
        if (legacyRadialRounding && metric === 'euclidean' && RADIAL_ROUND_UP.has(distanceSquared)) {
          alpha += direction === 'in' ? 1 : -1;
        }
      }
      const offset = (y * width + x) * 4;
      pixels[offset] = rgba[0];
      pixels[offset + 1] = rgba[1];
      pixels[offset + 2] = rgba[2];
      pixels[offset + 3] = Math.round((alpha * rgba[3]) / 255);
    }
  }
  return pixels;
}

export function renderLinearGradient({
  width,
  height,
  angle = 180,
  extent: explicitExtent,
  stops,
  easing = 'linear',
}: LinearGradientSpec): Uint8ClampedArray {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
    throw new Error('Linear gradient dimensions must be positive integers.');
  }
  if (stops.length < 2) throw new Error('At least two colour stops are required.');
  const orderedStops = stops.map((stop) => ({ ...stop, rgba: parseRgba(stop.color) })).sort((a, b) => a.offset - b.offset);
  if (orderedStops[0].offset < 0 || orderedStops[orderedStops.length - 1].offset > 1) {
    throw new Error('Stop offsets must be between zero and one.');
  }

  const radians = (angle * Math.PI) / 180;
  const snapDirection = (value: number): number => {
    if (Math.abs(value) < 1e-12) return 0;
    if (Math.abs(Math.abs(value) - 1) < 1e-12) return Math.sign(value);
    return value;
  };
  const directionX = snapDirection(Math.sin(radians));
  const directionY = snapDirection(-Math.cos(radians));
  const extent = (Math.abs(directionX) * Math.max(1, width - 1) + Math.abs(directionY) * Math.max(1, height - 1)) / 2 || 1;
  const length = explicitExtent ?? 2 * extent;
  if (!(length > 0)) throw new Error('Linear gradient extent must be positive.');
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const centeredX = width === 1 ? 0 : x - (width - 1) / 2;
      const centeredY = height === 1 ? 0 : y - (height - 1) / 2;
      const position = clamp01((centeredX * directionX + centeredY * directionY + extent) / length);
      let rightIndex = orderedStops.findIndex((stop) => stop.offset >= position);
      if (rightIndex < 0) rightIndex = orderedStops.length - 1;
      const leftIndex = Math.max(0, rightIndex - 1);
      const left = orderedStops[leftIndex];
      const right = orderedStops[rightIndex];
      const span = right.offset - left.offset;
      const rawAmount = span === 0 ? 0 : (position - left.offset) / span;
      const amount = applyEasing(clamp01(rawAmount), right.easing || left.easing || easing);
      const offset = (y * width + x) * 4;
      for (let channel = 0; channel < 4; channel += 1) {
        pixels[offset + channel] = Math.round(left.rgba[channel] * (1 - amount) + right.rgba[channel] * amount);
      }
    }
  }
  return pixels;
}

export function renderEllipticalGradient({
  width,
  height,
  centerX = (width - 1) / 2,
  centerY = (height - 1) / 2,
  radiusX,
  radiusY,
  rotation = 0,
  stops,
  easing = 'linear',
  legacyRadialRounding = false,
}: EllipticalGradientSpec): Uint8ClampedArray {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
    throw new Error('Ellipse dimensions must be positive integers.');
  }
  if (!(radiusX > 0) || !(radiusY > 0)) throw new Error('Ellipse radii must be positive.');
  if (stops.length < 2) throw new Error('At least two colour stops are required.');
  const orderedStops = stops.map((stop) => ({ ...stop, rgba: parseRgba(stop.color) })).sort((a, b) => a.offset - b.offset);
  if (orderedStops[0].offset < 0 || orderedStops[orderedStops.length - 1].offset > 1) {
    throw new Error('Stop offsets must be between zero and one.');
  }

  const cosine = Math.cos(rotation);
  const sine = Math.sin(rotation);
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const deltaX = x - centerX;
      const deltaY = y - centerY;
      const distanceSquared = deltaX ** 2 + deltaY ** 2;
      const rotatedX = deltaX * cosine + deltaY * sine;
      const rotatedY = -deltaX * sine + deltaY * cosine;
      const distance = clamp01(Math.hypot(rotatedX / radiusX, rotatedY / radiusY));
      let rightIndex = orderedStops.findIndex((stop) => stop.offset >= distance);
      if (rightIndex < 0) rightIndex = orderedStops.length - 1;
      const leftIndex = Math.max(0, rightIndex - 1);
      const left = orderedStops[leftIndex];
      const right = orderedStops[rightIndex];
      const span = right.offset - left.offset;
      const rawAmount = span === 0 ? 0 : (distance - left.offset) / span;
      const amount = applyEasing(clamp01(rawAmount), right.easing || left.easing || easing);
      const offset = (y * width + x) * 4;
      for (let channel = 0; channel < 4; channel += 1) {
        pixels[offset + channel] = Math.round(left.rgba[channel] * (1 - amount) + right.rgba[channel] * amount);
      }
      if (legacyRadialRounding && RADIAL_ROUND_UP.has(distanceSquared)) {
        pixels[offset + 3] += orderedStops[0].rgba[3] > orderedStops[orderedStops.length - 1].rgba[3] ? 1 : -1;
      }
    }
  }
  return pixels;
}

export interface RasterOperationResult extends RasterData {}

export function rotateRGBA90(pixels: PixelBuffer, width: number, height: number, turns = 1): RasterOperationResult {
  assertPixels(pixels, width, height);
  const normalizedTurns = ((turns % 4) + 4) % 4;
  if (normalizedTurns === 0) return { pixels: new Uint8ClampedArray(pixels), width, height };
  const outputWidth = normalizedTurns % 2 === 0 ? width : height;
  const outputHeight = normalizedTurns % 2 === 0 ? height : width;
  const output = new Uint8ClampedArray(pixels.length);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      let destinationX: number;
      let destinationY: number;
      if (normalizedTurns === 1) [destinationX, destinationY] = [height - 1 - y, x];
      else if (normalizedTurns === 2) [destinationX, destinationY] = [width - 1 - x, height - 1 - y];
      else [destinationX, destinationY] = [y, width - 1 - x];
      const sourceOffset = (y * width + x) * 4;
      const destinationOffset = (destinationY * outputWidth + destinationX) * 4;
      output.set(pixels.subarray(sourceOffset, sourceOffset + 4), destinationOffset);
    }
  }
  return { pixels: output, width: outputWidth, height: outputHeight };
}

function sinc(value: number): number {
  if (value === 0) return 1;
  const angle = Math.PI * value;
  return Math.sin(angle) / angle;
}

function lanczos(value: number): number {
  const absolute = Math.abs(value);
  return absolute < 3 ? sinc(value) * sinc(value / 3) : 0;
}

type WeightedSample = readonly [source: number, weight: number];

function contributions(sourceSize: number, destinationSize: number): WeightedSample[][] {
  const scale = sourceSize / destinationSize;
  const filterScale = Math.max(1, scale);
  const support = 3 * filterScale;
  return Array.from({ length: destinationSize }, (_, destination) => {
    const centre = (destination + 0.5) * scale - 0.5;
    const start = Math.ceil(centre - support);
    const end = Math.floor(centre + support);
    const weights = new Map<number, number>();
    let total = 0;
    for (let source = start; source <= end; source += 1) {
      const weight = lanczos((centre - source) / filterScale);
      if (weight === 0) continue;
      // ImageMagick-style resize clips the kernel at image edges, then
      // renormalizes the remaining samples instead of extending edge pixels.
      if (source < 0 || source >= sourceSize) continue;
      weights.set(source, weight);
      total += weight;
    }
    return [...weights].map(([source, weight]) => [source, weight / total] as const);
  });
}

export function resizeLanczos3RGBA(
  pixels: PixelBuffer,
  sourceWidth: number,
  sourceHeight: number,
  destinationWidth: number,
  destinationHeight: number,
): Uint8ClampedArray {
  assertPixels(pixels, sourceWidth, sourceHeight);
  if (!Number.isInteger(destinationWidth) || !Number.isInteger(destinationHeight) || destinationWidth < 1 || destinationHeight < 1) {
    throw new Error('Resize dimensions must be positive integers.');
  }
  const horizontalWeights = contributions(sourceWidth, destinationWidth);
  const verticalWeights = contributions(sourceHeight, destinationHeight);
  const horizontal = new Float64Array(destinationWidth * sourceHeight * 4);

  for (let y = 0; y < sourceHeight; y += 1) {
    for (let x = 0; x < destinationWidth; x += 1) {
      const outputOffset = (y * destinationWidth + x) * 4;
      for (const [sourceX, weight] of horizontalWeights[x]) {
        const sourceOffset = (y * sourceWidth + sourceX) * 4;
        const alpha = pixels[sourceOffset + 3] / 255;
        horizontal[outputOffset] += pixels[sourceOffset] * alpha * weight;
        horizontal[outputOffset + 1] += pixels[sourceOffset + 1] * alpha * weight;
        horizontal[outputOffset + 2] += pixels[sourceOffset + 2] * alpha * weight;
        horizontal[outputOffset + 3] += pixels[sourceOffset + 3] * weight;
      }
    }
  }

  const output = new Uint8ClampedArray(destinationWidth * destinationHeight * 4);
  for (let y = 0; y < destinationHeight; y += 1) {
    for (let x = 0; x < destinationWidth; x += 1) {
      const outputOffset = (y * destinationWidth + x) * 4;
      const accumulated: [number, number, number, number] = [0, 0, 0, 0];
      for (const [sourceY, weight] of verticalWeights[y]) {
        const sourceOffset = (sourceY * destinationWidth + x) * 4;
        for (let channel = 0; channel < 4; channel += 1) accumulated[channel] += horizontal[sourceOffset + channel] * weight;
      }
      const alpha = Math.min(255, Math.max(0, accumulated[3]));
      output[outputOffset + 3] = Math.round(alpha);
      if (alpha > 0) {
        output[outputOffset] = Math.round((accumulated[0] * 255) / alpha);
        output[outputOffset + 1] = Math.round((accumulated[1] * 255) / alpha);
        output[outputOffset + 2] = Math.round((accumulated[2] * 255) / alpha);
      }
    }
  }
  return output;
}
