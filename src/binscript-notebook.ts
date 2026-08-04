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
import type { ImageData, RecipeArtifact } from './recipe-executor.js';

export const STARTER_SOURCE = `import "bingen/basic"

size := 64

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


blocks := colors.map(fill).size(size)
blocks


top-down := lg(180deg, white, transparent).size(size)
bottom-up := lg(0deg, white, transparent).size(size)
left-right := lg(90deg, white, transparent).size(size)
right-left := lg(270deg, white, transparent).size(size)
[bottom-up, top-down, left-right, right-left]


top-down-colors := blocks.mask(top-down)
bottom-up-colors := blocks.mask(bottom-up)
left-right-colors := blocks.mask(left-right)
right-left-colors := blocks.mask(right-left)
[top-down-colors, bottom-up-colors, left-right-colors, right-left-colors]
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
    if (stream.match(/^(?:palette|fill|linear-gradient|lin-grad|lg|radial-gradient|rad-grad|rg|map|size|mask|preview)\b/))
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
    const input = dom.querySelector('input');
    if (!(input instanceof HTMLInputElement)) return false;
    if (document.activeElement !== input) input.value = this.value;
    return true;
  }

  toDOM(view: EditorView): HTMLElement {
    const label = document.createElement('label');
    label.className = 'binscript-color-control';
    label.title = `Edit ${this.value}`;
    const input = document.createElement('input');
    input.type = 'color';
    input.value = this.value;
    input.setAttribute('aria-label', `Edit color ${this.value}`);
    input.addEventListener('input', () => {
      view.dispatch({ changes: { from: this.from, to: this.to, insert: input.value } });
    });
    label.append(input);
    return label;
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
  stageId: string;
  images: readonly ImageData[];
  artifactCount: number;
}

const setStagePreviews = StateEffect.define<readonly StagePreview[]>();

function drawPreview(canvas: HTMLCanvasElement, image: ImageData): void {
  canvas.width = image.width;
  canvas.height = image.height;
  const context = canvas.getContext('2d');
  if (!context) return;
  context.putImageData(new globalThis.ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height), 0, 0);
}

class StagePreviewWidget extends WidgetType {
  constructor(private readonly preview: StagePreview) {
    super();
  }

  eq(other: StagePreviewWidget): boolean {
    return (
      this.preview.version === other.preview.version &&
      this.preview.rows.length === other.preview.rows.length &&
      this.preview.rows.every((row, index) => row.stageId === other.preview.rows[index]?.stageId)
    );
  }

  get estimatedHeight(): number {
    return this.preview.rows.length * 78;
  }

  toDOM(view: EditorView): HTMLElement {
    const group = document.createElement('div');
    group.className = 'binscript-preview-group';
    for (const row of this.preview.rows) group.append(this.rowDOM(row));
    queueMicrotask(() => view.requestMeasure());
    return group;
  }

  private rowDOM(row: StagePreviewRow): HTMLElement {
    const panel = document.createElement('section');
    panel.className = 'binscript-stage-preview';
    const strip = document.createElement('div');
    strip.className = 'binscript-preview-strip';
    for (const [index, image] of row.images.entries()) {
      const canvas = document.createElement('canvas');
      canvas.className = 'binscript-preview-canvas';
      canvas.setAttribute('aria-label', `${row.stageId} preview ${index + 1}`);
      drawPreview(canvas, image);
      strip.append(canvas);
    }
    if (row.artifactCount > row.images.length) {
      const remainder = document.createElement('span');
      remainder.className = 'binscript-preview-remainder';
      remainder.textContent = `+${row.artifactCount - row.images.length}`;
      strip.append(remainder);
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
        (target): target is Extract<BinScriptProjectionTarget, { kind: 'stage' }> => target.kind === 'stage',
      );
      const previews = targets.map((target) => {
        return {
          at: target.at,
          rows: target.stageIds.map((stageId) => {
            const artifacts = result.stages.get(stageId) ?? [];
            return {
              stageId,
              images: artifacts.slice(0, 8).map((artifact: RecipeArtifact) => artifact.image),
              artifactCount: artifacts.length,
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
