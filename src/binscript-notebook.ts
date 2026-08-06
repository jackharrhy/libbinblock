import { minimalSetup } from 'codemirror';
import { StreamLanguage } from '@codemirror/language';
import { setDiagnostics } from '@codemirror/lint';
import type { Diagnostic } from '@codemirror/lint';
import { StateEffect, StateField } from '@codemirror/state';
import { Decoration, EditorView, ViewPlugin, WidgetType } from '@codemirror/view';
import type { DecorationSet, ViewUpdate } from '@codemirror/view';
import { BinScriptError, compileBinScript } from './binscript-language.js';
import type { BinScriptProjectionTarget } from './binscript-language.js';
import { compileRecipe } from './recipe-executor.js';
import type { ImageData } from './recipe-executor.js';

const colorPickerReady = typeof window === 'undefined' ? undefined : import('vanilla-colorful/hex-alpha-color-picker.js');
const comparisonFixturesReady = typeof window === 'undefined' ? undefined : import('virtual:comparison-fixtures');
const comparisonFixtureCache = new Map<string, Promise<readonly { key: string; image: ImageData }[]>>();

async function decodePng(base64: string): Promise<ImageData> {
  const bytes = Uint8Array.from(atob(base64), (character) => character.charCodeAt(0));
  const bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
  const canvas = document.createElement('canvas');
  canvas.width = bitmap.width;
  canvas.height = bitmap.height;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('Could not decode comparison fixture.');
  context.drawImage(bitmap, 0, 0);
  bitmap.close();
  const image = context.getImageData(0, 0, canvas.width, canvas.height);
  return { width: image.width, height: image.height, pixels: new Uint8ClampedArray(image.data) };
}

function comparisonFixtures(fixtureSet: string): Promise<readonly { key: string; image: ImageData }[]> {
  let fixtures = comparisonFixtureCache.get(fixtureSet);
  if (!fixtures) {
    fixtures = (async () => {
      const module = await comparisonFixturesReady;
      if (!module) throw new Error('Comparison fixtures are only available in the browser.');
      const encoded = module.COMPARISON_FIXTURES[fixtureSet];
      if (!encoded) throw new Error(`Unknown comparison fixture set: ${fixtureSet}`);
      return Promise.all(encoded.map(async ({ key, base64 }) => ({ key, image: await decodePng(base64) })));
    })();
    comparisonFixtureCache.set(fixtureSet, fixtures);
  }
  return fixtures;
}

export const STARTER_SOURCE = `import "bingen/basic"

colors := palette(
  black-1: #000000,
  black-2: #808080,
  black-3: #c0c0c0,
  black-4: #ffffff,
  blue-lo: #000080,
  blue-hi: #0000ff,
  green-lo: #008000,
  green-hi: #00ff00,
  cyan-lo: #008080,
  cyan-hi: #00ffff,
  red-lo: #800000,
  red-hi: #ff0000,
  yellow-lo: #808000,
  yellow-hi: #ffff00,
  pink-lo: #800080,
  pink-hi: #ff00ff,
)

size := 64
blocks := colors.map(fill).size(size)
blocks

gradient-start := white
gradient-end := transparent-white
top-down := lg(180deg, gradient-start, gradient-end).size(size)
bottom-up := lg(0deg, gradient-start, gradient-end).size(size)
left-right := lg(90deg, gradient-start, gradient-end).size(size)
right-left := lg(270deg, gradient-start, gradient-end).size(size)
gradients := collect([bottom-up, top-down, left-right, right-left])
gradients

mask-black := black
mask-white := white
mask-transparent := transparent-black
mask-angle := 180deg
inner-size := 63
border-size := 62

mask-00 := lg(mask-angle, mask-black, mask-transparent, easing: "legacy", extent: size).size(size)
mask-01 := mask-00.rotate(2)
mask-02 := mask-00.rotate(3)
mask-03-full := mask-00.rotate(1)
mask-03 := mask-03-full.crop(0, 0, inner-size, size)

mask-04 := rg(circle, mask-black, mask-transparent, center: [31, 31], radius: 32, easing: "legacy", rounding: "legacy").size(inner-size)
mask-04-placed := mask-04.canvas(size, size, 1, 1)
mask-05 := rg(circle, mask-transparent, mask-black, center: [32, 32], radius: 32, easing: "legacy", rounding: "legacy").size(size)

mask-06 := square-gradient(center: [31, 31], radius: 32, direction: "in", easing: "legacy", color: mask-black).size(inner-size)
mask-06-placed := mask-06.canvas(size, size, 1, 1)
mask-07 := square-gradient(center: [32, 32], radius: 32, direction: "out", easing: "legacy", color: mask-black).size(size)

mask-08 := border-gradient(color: mask-black, levels: [53, 101, 146, 179, 206, 255]).size(border-size)

mask-09 := mask-00.rgb(mask-white)
mask-10-crop := mask-01.crop(0, 0, size, inner-size)
mask-10 := mask-10-crop.rgb(mask-white)
mask-11 := mask-02.rgb(mask-white)
mask-12 := mask-03.rgb(mask-white)
mask-13 := mask-04.rgb(mask-white)
mask-14 := mask-05.rgb(mask-white)
mask-15 := mask-07.rgb(mask-white)
mask-16 := mask-06.rgb(mask-white)
mask-17 := mask-08.rgb(mask-white)

mask-18-white-color := #ffffff70
mask-18-white-edge := #ffffff00
mask-18-black-color := #000000ff
mask-18-black-center := #00000000
mask-18-white := rg(circle, mask-18-white-color, mask-18-white-edge, center: [16, 18], radius: 45.2548, easing: "legacy").size(size)
mask-18-black := rg(circle, mask-18-black-center, mask-18-black-color, center: [25.29, 19.4], radius: 121.92, easing: "legacy").size(size)
mask-18 := mask-18-white.over(mask-18-black)

reference-shaped-masks := collect([mask-00, mask-01, mask-02, mask-03, mask-04, mask-05, mask-06, mask-07, mask-08, mask-09, mask-10, mask-11, mask-12, mask-13, mask-14, mask-15, mask-16, mask-17, mask-18])
mask-fixtures := fixtures("gradient-masks")
mask-comparison := compare(reference-shaped-masks, mask-fixtures)
mask-comparison

placed-03 := mask-03.canvas(size, size, 1, 0)
placed-08 := mask-08.canvas(size, size, 1, 1)
placed-10 := mask-10.canvas(size, size, 0, 1)
placed-12 := mask-12.canvas(size, size, 1, 0)
placed-13 := mask-13.canvas(size, size, 1, 1)
placed-16 := mask-16.canvas(size, size, 1, 1)
placed-17 := mask-17.canvas(size, size, 1, 1)

masks := collect([mask-00, mask-01, mask-02, placed-03, mask-04-placed, mask-05, mask-06-placed, mask-07, placed-08, mask-09, placed-10, mask-11, placed-12, placed-13, mask-14, mask-15, placed-16, placed-17, mask-18])
masks

variant-blue-hi := fill(#0000ff).size(size)
grad00-field := rg(circle, mask-black 64%, mask-transparent, center: [31.5, 31.5], radius: 32).size(size)
grad01-field := rg(circle, mask-transparent 32%, mask-black, center: [31.5, 31.5], radius: 32).size(size)
grad02-field := rg(circle, mask-black 32%, mask-transparent, center: [31.5, 31.5], radius: 32).size(size)
grad03-field := rg(circle, mask-transparent, mask-black, center: [31.5, 31.5], radius: 32).size(size)
grad04-field := rg(circle, mask-black, mask-transparent, center: [31.5, 31.5], radius: 32).size(size)
grad00-white-field := grad00-field.rgb(mask-white)
grad01-white-field := grad01-field.rgb(mask-white)
grad02-white-field := grad02-field.rgb(mask-white)
grad03-white-field := grad03-field.rgb(mask-white)
grad04-white-field := grad04-field.rgb(mask-white)

blue-hi-grad00blk100 := variant-blue-hi.over(grad00-field)
blue-hi-grad00blk25 := variant-blue-hi.over(grad00-field, 0, 0, 0.25)
blue-hi-grad00wht := variant-blue-hi.over(grad00-white-field)
blue-hi-grad01blk100 := variant-blue-hi.over(grad01-field)
blue-hi-grad01blk25 := variant-blue-hi.over(grad01-field, 0, 0, 0.25)
blue-hi-grad01wht := variant-blue-hi.over(grad01-white-field)
blue-hi-grad02blk100 := variant-blue-hi.over(grad02-field)
blue-hi-grad02blk50 := variant-blue-hi.over(grad02-field, 0, 0, 0.5)
blue-hi-grad02wht := variant-blue-hi.over(grad02-white-field)
blue-hi-grad03blk100 := variant-blue-hi.over(grad03-field)
blue-hi-grad03blk25 := variant-blue-hi.over(grad03-field, 0, 0, 0.25)
blue-hi-grad03blk50 := variant-blue-hi.over(grad03-field, 0, 0, 0.5)
blue-hi-grad03wht100 := variant-blue-hi.over(grad03-white-field)
blue-hi-grad04blk100 := variant-blue-hi.over(grad04-field)
blue-hi-grad04blk50 := variant-blue-hi.over(grad04-field, 0, 0, 0.5)
blue-hi-grad04wht := variant-blue-hi.over(grad04-white-field)

blue-hi-variant-slice := collect([blue-hi-grad00blk100, blue-hi-grad00blk25, blue-hi-grad00wht, blue-hi-grad01blk100, blue-hi-grad01blk25, blue-hi-grad01wht, blue-hi-grad02blk100, blue-hi-grad02blk50, blue-hi-grad02wht, blue-hi-grad03blk100, blue-hi-grad03blk25, blue-hi-grad03blk50, blue-hi-grad03wht100, blue-hi-grad04blk100, blue-hi-grad04blk50, blue-hi-grad04wht])
blue-hi-variant-slice
variant-fixtures := fixtures("gradient-variants-blue-hi")
variant-comparison := compare(blue-hi-variant-slice, variant-fixtures)
variant-comparison

alpha-variants := blocks.mask(masks)
alpha-variants

small-alpha-variants := alpha-variants.resize(8)
small-alpha-variants
`;

export const STARTER_RECIPE = compileBinScript(STARTER_SOURCE).document;

export function recipeProjectionTargets(source: string): BinScriptProjectionTarget[] {
  return compileBinScript(source).projections;
}

const binscriptLanguage = StreamLanguage.define<null>({
  startState: () => null,
  token(stream) {
    if (stream.eatSpace()) return null;
    if (stream.match('//')) {
      stream.skipToEnd();
      return 'comment';
    }
    if (stream.match(/^#[\da-fA-F]{6}(?:[\da-fA-F]{2})?/)) return 'color';
    if (stream.match(/^-?(?:\d+(?:\.\d*)?|\.\d+)(?:%|deg)?/)) return 'number';
    if (stream.match(/^import\b/)) return 'keyword';
    if (stream.match(/^"(?:[^"\\]|\\.)*"/)) return 'string';
    if (
      stream.match(
        /^(?:palette|fill|square-gradient|border-gradient|linear-gradient|lin-grad|lg|radial-gradient|rad-grad|rg|fixtures|compare|collect|union|product|map|select|slice|size|resize|rotate|opacity|tint|rgb|invert-alpha|crop|canvas|mask|over|preview)\b/,
      )
    )
      return 'variableName';
    if (stream.match(/^[A-Za-z_][\w-]*/)) return 'variableName';
    if (stream.match(':=') || stream.match(/[.,:()]/) || stream.match('[') || stream.match(']')) return 'operator';
    stream.next();
    return null;
  },
});

class ColorWidget extends WidgetType {
  constructor(
    private readonly from: number,
    private readonly to: number,
    private readonly value: string,
  ) {
    super();
  }

  eq(other: ColorWidget): boolean {
    return this.value === other.value && this.from === other.from && this.to === other.to;
  }

  updateDOM(dom: HTMLElement): boolean {
    const picker = dom.querySelector('hex-alpha-color-picker');
    const input = dom.querySelector<HTMLInputElement>('.binscript-color-value');
    const swatch = dom.querySelector<HTMLElement>('.binscript-color-swatch-value');
    if (!picker || !input || !swatch) return false;
    const color = this.rgba();
    dom.dataset.from = String(this.from);
    dom.dataset.to = String(this.to);
    dom.dataset.value = color;
    if (customElements.get('hex-alpha-color-picker') && picker.color !== color) picker.color = color;
    if (document.activeElement !== input) input.value = color;
    swatch.style.backgroundColor = color;
    dom.title = `Edit ${color}`;
    return true;
  }

  toDOM(view: EditorView): HTMLElement {
    const control = document.createElement('span');
    control.className = 'binscript-color-control';
    control.dataset.from = String(this.from);
    control.dataset.to = String(this.to);
    control.dataset.value = this.rgba();
    control.title = `Edit ${this.rgba()}`;

    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'binscript-color-swatch';
    button.setAttribute('aria-label', `Edit color ${this.rgba()}`);
    const swatch = document.createElement('span');
    swatch.className = 'binscript-color-swatch-value';
    swatch.style.backgroundColor = this.rgba();
    button.append(swatch);

    const panel = document.createElement('div');
    panel.className = 'binscript-color-popover';
    panel.setAttribute('popover', 'auto');
    const picker = document.createElement('hex-alpha-color-picker');
    void colorPickerReady?.then(() => {
      picker.color = control.dataset.value ?? this.rgba();
    });
    const input = document.createElement('input');
    input.className = 'binscript-color-value';
    input.value = this.rgba();
    input.setAttribute('aria-label', 'RGBA hexadecimal color');

    const commit = (color: string): void => {
      if (!/^#[\da-f]{8}$/i.test(color)) return;
      const normalized = color.toLowerCase();
      control.dataset.value = normalized;
      input.value = normalized;
      swatch.style.backgroundColor = normalized;
      const alpha = normalized.slice(7);
      const insert = alpha === 'ff' ? normalized.slice(0, 7) : normalized;
      view.dispatch({
        changes: {
          from: Number(control.dataset.from),
          to: Number(control.dataset.to),
          insert,
        },
      });
    };
    picker.addEventListener('color-changed', (event: Event) => {
      commit((event as CustomEvent<{ value: string }>).detail.value);
    });
    input.addEventListener('change', () => {
      const value = input.value.startsWith('#') ? input.value : `#${input.value}`;
      const color = value.length === 7 ? `${value}ff` : value;
      if (!/^#[\da-f]{8}$/i.test(color)) {
        input.value = control.dataset.value ?? this.rgba();
        return;
      }
      picker.color = color;
      commit(color);
    });

    button.addEventListener('click', () => {
      if (panel.matches(':popover-open')) {
        panel.hidePopover();
        return;
      }
      const bounds = button.getBoundingClientRect();
      panel.style.left = `${Math.max(8, Math.min(bounds.left, window.innerWidth - 236))}px`;
      panel.style.top = `${Math.max(8, Math.min(bounds.bottom + 6, window.innerHeight - 286))}px`;
      panel.showPopover();
    });

    panel.append(picker, input);
    control.append(button, panel);
    return control;
  }

  private rgba(): string {
    return this.value.length === 9 ? this.value.toLowerCase() : `${this.value.toLowerCase()}ff`;
  }

  ignoreEvent(): boolean {
    return true;
  }
}

class NumberWidget extends WidgetType {
  constructor(private readonly target: Extract<BinScriptProjectionTarget, { kind: 'number' }>) {
    super();
  }

  eq(other: NumberWidget): boolean {
    return this.target.value === other.target.value && this.target.from === other.target.from && this.target.to === other.target.to;
  }

  updateDOM(dom: HTMLElement): boolean {
    const input = dom.querySelector('input');
    if (!(input instanceof HTMLInputElement)) return false;
    if (document.activeElement !== input) input.value = String(this.target.value);
    dom.title = `${this.target.property}: ${this.target.value}`;
    return true;
  }

  toDOM(view: EditorView): HTMLElement {
    const label = document.createElement('label');
    label.className = 'binscript-number-control';
    label.title = `${this.target.property}: ${this.target.value}`;
    const input = document.createElement('input');
    input.type = 'range';
    input.min = String(this.target.min);
    input.max = String(this.target.max);
    input.step = String(this.target.step);
    input.value = String(this.target.value);
    input.setAttribute('aria-label', `Adjust ${this.target.property}, current value ${this.target.value}`);
    input.addEventListener('input', () => {
      label.title = `${this.target.property}: ${input.value}`;
      input.setAttribute('aria-label', `Adjust ${this.target.property}, current value ${input.value}`);
      view.dispatch({ changes: { from: this.target.from, to: this.target.to, insert: `${input.value}${this.target.suffix}` } });
    });
    label.append(input);
    return label;
  }

  ignoreEvent(): boolean {
    return true;
  }
}

function controlDecorations(view: EditorView): DecorationSet {
  let targets: BinScriptProjectionTarget[];
  try {
    targets = recipeProjectionTargets(view.state.doc.toString());
  } catch {
    return Decoration.none;
  }
  const decorations = targets.flatMap((target) => {
    if (target.kind === 'color')
      return [Decoration.widget({ widget: new ColorWidget(target.from, target.to, target.value), side: 1 }).range(target.to)];
    if (target.kind === 'number') return [Decoration.widget({ widget: new NumberWidget(target), side: 1 }).range(target.to)];
    return [];
  });
  return Decoration.set(decorations, true);
}

const controlPlugin = ViewPlugin.fromClass(
  class {
    decorations: DecorationSet;

    constructor(view: EditorView) {
      this.decorations = controlDecorations(view);
    }

    update(update: ViewUpdate): void {
      if (update.docChanged) this.decorations = controlDecorations(update.view);
    }
  },
  { decorations: (plugin) => plugin.decorations },
);

interface StagePreview {
  at: number;
  rows: StagePreviewRow[];
  version: number;
}

interface StagePreviewRow {
  stageIds: string[];
  images: StagePreviewImage[];
}

interface StagePreviewImage {
  stageId: string;
  image: ImageData;
  index: number;
  reference?: ImageData;
  difference?: ImageData;
  metrics?: string;
}

const setStagePreviews = StateEffect.define<readonly StagePreview[]>();

function drawPreview(canvas: HTMLCanvasElement, image: ImageData): void {
  canvas.width = image.width;
  canvas.height = image.height;
  const context = canvas.getContext('2d');
  if (!context) return;
  context.putImageData(new globalThis.ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height), 0, 0);
}

function compareImages(generated: ImageData, reference: ImageData): { difference: ImageData; metrics: string } {
  if (generated.width !== reference.width || generated.height !== reference.height) {
    return {
      difference: {
        width: generated.width,
        height: generated.height,
        pixels: new Uint8ClampedArray(generated.width * generated.height * 4),
      },
      metrics: `${generated.width}×${generated.height} vs ${reference.width}×${reference.height}`,
    };
  }
  const pixels = new Uint8ClampedArray(generated.pixels.length);
  let squaredError = 0;
  let maximumError = 0;
  let changedPixels = 0;
  for (let offset = 0; offset < pixels.length; offset += 4) {
    let pixelError = 0;
    for (let channel = 0; channel < 4; channel += 1) {
      const error = Math.abs(generated.pixels[offset + channel] - reference.pixels[offset + channel]);
      squaredError += error ** 2;
      maximumError = Math.max(maximumError, error);
      pixelError = Math.max(pixelError, error);
    }
    if (pixelError > 0) changedPixels += 1;
    pixels[offset] = Math.min(255, pixelError * 24);
    pixels[offset + 1] = pixelError === 0 ? 0 : Math.min(96, pixelError * 4);
    pixels[offset + 2] = 0;
    pixels[offset + 3] = 255;
  }
  const rmse = Math.sqrt(squaredError / generated.pixels.length);
  return {
    difference: { width: generated.width, height: generated.height, pixels },
    metrics: `${changedPixels}px · max ${maximumError} · rmse ${rmse.toFixed(2)}`,
  };
}

class StagePreviewWidget extends WidgetType {
  constructor(private readonly preview: StagePreview) {
    super();
  }

  eq(other: StagePreviewWidget): boolean {
    return (
      this.preview.version === other.preview.version &&
      this.preview.rows.length === other.preview.rows.length &&
      this.preview.rows.every((row, index) => row.stageIds.join('\0') === other.preview.rows[index]?.stageIds.join('\0'))
    );
  }

  get estimatedHeight(): number {
    return this.preview.rows.length * (this.uniformDimensions() ? 76 : 89) + 2;
  }

  toDOM(view: EditorView): HTMLElement {
    const group = document.createElement('div');
    group.className = 'binscript-preview-group';
    const uniformDimensions = this.uniformDimensions();
    if (uniformDimensions) {
      group.classList.add('binscript-preview-group-uniform');
      const label = document.createElement('span');
      label.className = 'binscript-preview-group-dimensions';
      label.textContent = uniformDimensions;
      group.append(label);
    }
    for (const row of this.preview.rows) group.append(this.rowDOM(row, !uniformDimensions));
    queueMicrotask(() => view.requestMeasure());
    return group;
  }

  private uniformDimensions(): string | undefined {
    const dimensions = new Set(this.preview.rows.flatMap((row) => row.images.map(({ image }) => `${image.width}×${image.height}`)));
    return dimensions.size === 1 ? dimensions.values().next().value : undefined;
  }

  private rowDOM(row: StagePreviewRow, showIndividualDimensions: boolean): HTMLElement {
    const panel = document.createElement('section');
    panel.className = 'binscript-stage-preview';
    const strip = document.createElement('div');
    strip.className = 'binscript-preview-strip';
    for (const preview of row.images) {
      const item = document.createElement('div');
      item.className = 'binscript-preview-item';
      const dimensions = `${preview.image.width}×${preview.image.height}`;
      if (preview.reference && preview.difference) {
        item.classList.add('binscript-comparison-item');
        const images = document.createElement('div');
        images.className = 'binscript-comparison-images';
        for (const [labelText, image] of [
          ['generated', preview.image],
          ['reference', preview.reference],
          ['difference', preview.difference],
        ] as const) {
          const figure = document.createElement('figure');
          const canvas = document.createElement('canvas');
          canvas.className = 'binscript-preview-canvas';
          canvas.setAttribute('aria-label', `${preview.stageId} ${labelText}, ${dimensions}`);
          drawPreview(canvas, image);
          const caption = document.createElement('figcaption');
          caption.textContent = labelText;
          figure.append(canvas, caption);
          images.append(figure);
        }
        const metrics = document.createElement('span');
        metrics.className = 'binscript-comparison-metrics';
        metrics.textContent = `${preview.stageId} · ${preview.metrics}`;
        item.append(images, metrics);
      } else {
        const canvas = document.createElement('canvas');
        canvas.className = 'binscript-preview-canvas';
        canvas.setAttribute('aria-label', `${preview.stageId} preview ${preview.index + 1}, ${dimensions}`);
        canvas.title = dimensions;
        drawPreview(canvas, preview.image);
        item.append(canvas);
      }
      if (showIndividualDimensions) {
        const label = document.createElement('span');
        label.className = 'binscript-preview-dimensions';
        label.textContent = dimensions;
        item.append(label);
      }
      strip.append(item);
    }
    panel.append(strip);
    return panel;
  }
}

const previewField = StateField.define<DecorationSet>({
  create: () => Decoration.none,
  update(previews, transaction) {
    let next = previews.map(transaction.changes);
    for (const effect of transaction.effects) {
      if (effect.is(setStagePreviews)) {
        next = Decoration.set(
          effect.value.map((preview) =>
            Decoration.widget({ widget: new StagePreviewWidget(preview), block: true, side: 0 }).range(preview.at),
          ),
          true,
        );
      }
    }
    return next;
  },
  provide: (field) => EditorView.decorations.from(field),
});

export function errorSummary(error: unknown): string {
  if (error && typeof error === 'object' && 'issues' in error && Array.isArray(error.issues)) {
    const issue = error.issues[0] as { path?: unknown; message?: unknown } | undefined;
    if (issue) {
      const path = Array.isArray(issue.path) ? issue.path.join('.') : '';
      const message = typeof issue.message === 'string' ? issue.message : 'Invalid recipe';
      return path ? `${path}: ${message}` : message;
    }
  }
  return error instanceof Error ? error.message.split('\n')[0] : String(error);
}

function diagnosticFor(error: unknown, documentLength: number): Diagnostic {
  const message = errorSummary(error);
  if (error instanceof BinScriptError) {
    const from = Math.min(error.from, Math.max(0, documentLength - 1));
    return {
      from,
      to: Math.min(Math.max(error.to, from + 1), documentLength),
      severity: 'error',
      message,
    };
  }
  const position = /position (\d+)/i.exec(message)?.[1];
  const from = position ? Math.min(Number(position), Math.max(0, documentLength - 1)) : 0;
  return {
    from,
    to: Math.min(documentLength, from + 1),
    severity: 'error',
    message,
  };
}

export interface RecipeNotebookOptions {
  parent: HTMLElement;
  initialSource?: string;
}

export interface RecipeNotebook {
  view: EditorView;
}

export function createRecipeNotebook({ parent, initialSource = STARTER_SOURCE }: RecipeNotebookOptions): RecipeNotebook {
  let evaluationVersion = 0;
  let timer: number | undefined;

  const run = async (): Promise<void> => {
    const source = view.state.doc.toString();
    const version = ++evaluationVersion;
    try {
      const compiled = compileBinScript(source);
      const result = await compileRecipe(compiled.document, { maximumArtifacts: 2_000 });
      if (version !== evaluationVersion || source !== view.state.doc.toString()) return;
      const targets = compiled.projections.filter(
        (target): target is Extract<BinScriptProjectionTarget, { kind: 'stage' | 'comparison' }> =>
          target.kind === 'stage' || target.kind === 'comparison',
      );
      const fixtureSets = new Map(
        await Promise.all(
          [...new Set(targets.filter((target) => target.kind === 'comparison').map((target) => target.fixtureSet))].map(
            async (fixtureSet) => [fixtureSet, await comparisonFixtures(fixtureSet)] as const,
          ),
        ),
      );
      const previews = targets.map((target) => {
        let fixtureIndex = 0;
        return {
          at: target.at,
          rows: target.stageRows.map((stageIds) => {
            const artifacts = stageIds.flatMap((stageId) =>
              (result.stages.get(stageId) ?? []).map((artifact, index) => ({ artifact, stageId, index })),
            );
            return {
              stageIds,
              images: artifacts.map(({ artifact, stageId, index }) => {
                const fixture = target.kind === 'comparison' ? fixtureSets.get(target.fixtureSet)?.[fixtureIndex++] : undefined;
                const reference = fixture?.image;
                const comparison = reference ? compareImages(artifact.image, reference) : undefined;
                return {
                  stageId,
                  image: artifact.image,
                  index,
                  ...(reference && comparison ? { reference, difference: comparison.difference, metrics: comparison.metrics } : {}),
                };
              }),
            };
          }),
          version,
        };
      });
      view.dispatch({ effects: setStagePreviews.of(previews) }, setDiagnostics(view.state, []));
    } catch (error: unknown) {
      if (version !== evaluationVersion || source !== view.state.doc.toString()) return;
      view.dispatch({ effects: setStagePreviews.of([]) }, setDiagnostics(view.state, [diagnosticFor(error, view.state.doc.length)]));
    }
  };

  const scheduleRun = (): void => {
    if (timer !== undefined) window.clearTimeout(timer);
    timer = window.setTimeout(() => void run(), 220);
  };

  const view = new EditorView({
    parent,
    doc: initialSource,
    extensions: [
      minimalSetup,
      binscriptLanguage,
      controlPlugin,
      previewField,
      EditorView.lineWrapping,
      EditorView.updateListener.of((update) => {
        if (update.docChanged) scheduleRun();
      }),
      EditorView.theme({
        '&': { fontSize: '14px' },
        '.cm-content': { fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace', padding: '18px 16px 96px' },
        '&.cm-focused': { outline: 'none' },
      }),
    ],
  });

  void run();

  return { view };
}
