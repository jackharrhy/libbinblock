// Historical behavioral oracle only. Production pixels are defined by libbinblock-raster.
export type RGB = readonly [red: number, green: number, blue: number];
export type PixelBuffer = Uint8Array | Uint8ClampedArray;
export type GradientAlpha = (x: number, y: number) => number;

export interface GradientPreset {
  label: string;
  archiveName: string;
  alpha: GradientAlpha;
}

export const PRESETS = {
  'top-down': { label: 'Top to bottom', archiveName: 'grad00', alpha: (x, y) => 1 - y },
  'bottom-up': { label: 'Bottom to top', archiveName: 'grad01', alpha: (x, y) => y },
  'left-right': { label: 'Left to right', archiveName: 'grad02', alpha: (x) => 1 - x },
  'right-left': { label: 'Right to left', archiveName: 'grad03', alpha: (x) => x },
  'radial-in': {
    label: 'Radial, centre in',
    archiveName: 'grad04',
    alpha: (x, y) => Math.max(0, 1 - Math.hypot(x - 0.5, y - 0.5) / Math.SQRT1_2),
  },
  'radial-out': {
    label: 'Radial, centre out',
    archiveName: 'grad05',
    alpha: (x, y) => Math.min(1, Math.hypot(x - 0.5, y - 0.5) / Math.SQRT1_2),
  },
  diagonal: { label: 'Diagonal', archiveName: 'grad06', alpha: (x, y) => 1 - (x + y) / 2 },
  corner: { label: 'Corner falloff', archiveName: 'grad07', alpha: (x, y) => 1 - Math.max(x, y) },
} as const satisfies Record<string, GradientPreset>;

export type PresetName = keyof typeof PRESETS;
export type PaletteEntry = readonly [id: string, color: string];

export const DEFAULT_PALETTE: readonly PaletteEntry[] = [
  ['col_black_1', '#000000'],
  ['col_black_2', '#808080'],
  ['col_black_3', '#c0c0c0'],
  ['col_black_4', '#ffffff'],
  ['col_blue_lo', '#000080'],
  ['col_blue_hi', '#0000ff'],
  ['col_green_lo', '#008000'],
  ['col_green_hi', '#00ff00'],
  ['col_cyan_lo', '#008080'],
  ['col_cyan_hi', '#00ffff'],
  ['col_red_lo', '#800000'],
  ['col_red_hi', '#ff0000'],
  ['col_yellow_lo', '#808000'],
  ['col_yellow_hi', '#ffff00'],
  ['col_pink_lo', '#800080'],
  ['col_pink_hi', '#ff00ff'],
];

export function parseHex(value: string): RGB {
  const hex = value.replace(/^#/, '');
  if (!/^[\da-f]{6}$/i.test(hex)) throw new Error(`Invalid RGB color: ${value}`);
  return [Number.parseInt(hex.slice(0, 2), 16), Number.parseInt(hex.slice(2, 4), 16), Number.parseInt(hex.slice(4, 6), 16)];
}

export function toHex([red, green, blue]: RGB): string {
  return `#${[red, green, blue].map((channel) => channel.toString(16).padStart(2, '0')).join('')}`;
}

function rotate(x: number, y: number, turns: number): readonly [number, number] {
  if (turns === 1) return [1 - y, x];
  if (turns === 2) return [1 - x, 1 - y];
  if (turns === 3) return [y, 1 - x];
  return [x, y];
}

export interface RenderBlockOptions {
  width?: number;
  height?: number;
  base?: string;
  overlay?: string;
  opacity?: number;
  preset?: PresetName;
  rotation?: number;
}

export function renderBlock({
  width = 64,
  height = 64,
  base = '#00ffff',
  overlay = '#000000',
  opacity = 1,
  preset = 'radial-in',
  rotation = 0,
}: RenderBlockOptions = {}): Uint8ClampedArray {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
    throw new Error('Dimensions must be positive integers.');
  }
  if (!(preset in PRESETS)) throw new Error(`Unknown gradient preset: ${preset}`);
  if (!Number.isFinite(opacity) || opacity < 0 || opacity > 1) throw new Error('Opacity must be between 0 and 1.');

  const baseRgb = parseHex(base);
  const overlayRgb = parseHex(overlay);
  const pixels = new Uint8ClampedArray(width * height * 4);
  const turns = ((rotation % 4) + 4) % 4;

  for (let row = 0; row < height; row += 1) {
    for (let column = 0; column < width; column += 1) {
      const x = width === 1 ? 0.5 : column / (width - 1);
      const y = height === 1 ? 0.5 : row / (height - 1);
      const [rotatedX, rotatedY] = rotate(x, y, turns);
      const alpha = Math.min(1, Math.max(0, PRESETS[preset].alpha(rotatedX, rotatedY) * opacity));
      const offset = (row * width + column) * 4;
      for (let channel = 0; channel < 3; channel += 1) {
        pixels[offset + channel] = Math.round(baseRgb[channel] * (1 - alpha) + overlayRgb[channel] * alpha);
      }
      pixels[offset + 3] = 255;
    }
  }
  return pixels;
}

export interface RenderMaskOptions {
  width?: number;
  height?: number;
  preset?: PresetName;
  rotation?: number;
  white?: boolean;
}

export function renderMask({
  width = 64,
  height = 64,
  preset = 'radial-in',
  rotation = 0,
  white = false,
}: RenderMaskOptions = {}): Uint8ClampedArray {
  if (!(preset in PRESETS)) throw new Error(`Unknown gradient preset: ${preset}`);
  const pixels = new Uint8ClampedArray(width * height * 4);
  const turns = ((rotation % 4) + 4) % 4;
  const colour = white ? 255 : 0;

  for (let row = 0; row < height; row += 1) {
    for (let column = 0; column < width; column += 1) {
      const x = width === 1 ? 0.5 : column / (width - 1);
      const y = height === 1 ? 0.5 : row / (height - 1);
      const [rotatedX, rotatedY] = rotate(x, y, turns);
      const offset = (row * width + column) * 4;
      pixels[offset] = colour;
      pixels[offset + 1] = colour;
      pixels[offset + 2] = colour;
      pixels[offset + 3] = Math.round(Math.min(1, Math.max(0, PRESETS[preset].alpha(rotatedX, rotatedY))) * 255);
    }
  }
  return pixels;
}
