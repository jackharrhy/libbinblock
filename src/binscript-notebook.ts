import { basicSetup } from 'codemirror';
import { json, jsonLanguage } from '@codemirror/lang-json';
import { lintGutter, setDiagnostics } from '@codemirror/lint';
import type { Diagnostic } from '@codemirror/lint';
import { StateEffect, StateField } from '@codemirror/state';
import { Decoration, EditorView, keymap, ViewPlugin, WidgetType } from '@codemirror/view';
import type { DecorationSet, ViewUpdate } from '@codemirror/view';
import { compileRecipe } from './recipe-executor.js';
import type { ImageData, RecipeArtifact } from './recipe-executor.js';
import type { RecipeDocument } from './recipe-schema.js';

const NUMBER_CONTROLS: Readonly<Record<string, { min: number; max: number; step: number }>> = {
  width: { min: 1, max: 256, step: 1 },
  height: { min: 1, max: 256, step: 1 },
  opacity: { min: 0, max: 1, step: 0.05 },
  rotation: { min: -4, max: 4, step: 1 },
  radiusX: { min: 0.01, max: 256, step: 0.05 },
  radiusY: { min: 0.01, max: 256, step: 0.05 },
  offsetX: { min: -256, max: 256, step: 1 },
  offsetY: { min: -256, max: 256, step: 1 },
};

export const STARTER_RECIPE: RecipeDocument = {
  format: 'bin-block-recipe/v1',
  profile: 'numeric-srgb/v1',
  metadata: {
    name: 'BinScript notebook starter',
  },
  values: {},
  assets: {},
  definitions: {},
  stages: [
    {
      type: 'render',
      id: 'base-colors',
      forEach: {
        color: {
          source: 'values',
          values: [
            { id: 'coral', value: '#ff6030' },
            { id: 'cyan', value: '#10d9d2' },
          ],
        },
      },
      key: ['get', 'id', ['var', 'color']],
      path: ['concat', 'notebook/base/', ['get', 'id', ['var', 'color']], '.png'],
      properties: {
        color: ['get', 'value', ['var', 'color']],
      },
      image: {
        op: 'fill',
        width: 64,
        height: 64,
        color: ['get', 'value', ['var', 'color']],
      },
    },
    {
      type: 'render',
      id: 'gradient-fields',
      forEach: {
        preset: {
          source: 'values',
          values: ['radial-in', 'diagonal'],
        },
      },
      key: ['var', 'preset'],
      path: ['concat', 'notebook/fields/', ['var', 'preset'], '.png'],
      properties: {
        preset: ['var', 'preset'],
      },
      image: {
        op: 'gradient',
        shape: 'preset',
        width: 64,
        height: 64,
        preset: ['var', 'preset'],
        rotation: 0,
        color: '#ffffff',
      },
    },
    {
      type: 'render',
      id: 'masked-tiles',
      forEach: {
        color: { source: 'stage', stage: 'base-colors' },
        field: { source: 'stage', stage: 'gradient-fields' },
      },
      key: ['concat', ['get', 'key', ['var', 'color']], '-', ['get', 'key', ['var', 'field']]],
      path: ['concat', 'notebook/tiles/', ['get', 'key', ['var', 'color']], '-', ['get', 'key', ['var', 'field']], '.png'],
      properties: {},
      image: {
        op: 'apply-mask',
        source: { op: 'input', binding: 'color' },
        mask: { op: 'input', binding: 'field' },
        mode: 'replace',
      },
    },
  ],
  outputs: [{ stage: 'masked-tiles' }],
};

export const STARTER_SOURCE = JSON.stringify(STARTER_RECIPE, null, 2);

export type ProjectionTarget =
  | { kind: 'color'; from: number; to: number; value: string }
  | { kind: 'number'; from: number; to: number; value: number; property: string; min: number; max: number; step: number }
  | { kind: 'stage'; from: number; to: number; at: number; stageId: string };

function parsedNodeValue(source: string, from: number, to: number): unknown {
  try {
    return JSON.parse(source.slice(from, to));
  } catch {
    return undefined;
  }
}

function propertyName(source: string, from: number, to: number): string | undefined {
  const propertySource = source.slice(from, to);
  const match = /^\s*"([^"]+)"\s*:/.exec(propertySource);
  return match?.[1];
}

export function recipeProjectionTargets(source: string): ProjectionTarget[] {
  const targets: ProjectionTarget[] = [];
  const tree = jsonLanguage.parser.parse(source);
  tree.iterate({
    enter(node) {
      if (node.name === 'String') {
        const value = parsedNodeValue(source, node.from, node.to);
        if (typeof value === 'string' && /^#[\da-f]{6}$/i.test(value)) {
          targets.push({ kind: 'color', from: node.from, to: node.to, value });
        }
        return;
      }
      if (node.name === 'Number' && node.node.parent?.name === 'Property') {
        const name = propertyName(source, node.node.parent.from, node.node.parent.to);
        const control = name ? NUMBER_CONTROLS[name] : undefined;
        const value = parsedNodeValue(source, node.from, node.to);
        if (control && typeof value === 'number') {
          targets.push({ kind: 'number', from: node.from, to: node.to, value, property: name!, ...control });
        }
        return;
      }
      if (node.name !== 'Object') return;
      const value = parsedNodeValue(source, node.from, node.to);
      if (value && typeof value === 'object' && 'type' in value && 'id' in value) {
        const candidate = value as { type?: unknown; id?: unknown };
        if (candidate.type === 'render' && typeof candidate.id === 'string') {
          const lineBreak = source.indexOf('\n', node.to);
          const at = lineBreak === -1 ? source.length : lineBreak + 1;
          targets.push({ kind: 'stage', from: node.from, to: node.to, at, stageId: candidate.id });
        }
      }
    },
  });
  return targets.sort((left, right) => left.from - right.from || left.to - right.to);
}

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

  toDOM(view: EditorView): HTMLElement {
    const label = document.createElement('label');
    label.className = 'binscript-color-control';
    label.title = `Edit ${this.value}`;
    const input = document.createElement('input');
    input.type = 'color';
    input.value = this.value;
    input.setAttribute('aria-label', `Edit color ${this.value}`);
    input.addEventListener('input', () => {
      view.dispatch({ changes: { from: this.from, to: this.to, insert: JSON.stringify(input.value) } });
    });
    label.append(input);
    return label;
  }

  ignoreEvent(): boolean {
    return false;
  }
}

class NumberWidget extends WidgetType {
  constructor(private readonly target: Extract<ProjectionTarget, { kind: 'number' }>) {
    super();
  }

  eq(other: NumberWidget): boolean {
    return this.target.value === other.target.value && this.target.from === other.target.from && this.target.to === other.target.to;
  }

  toDOM(view: EditorView): HTMLElement {
    const label = document.createElement('label');
    label.className = 'binscript-number-control';
    label.title = `Edit ${this.target.property}`;
    const input = document.createElement('input');
    input.type = this.target.property === 'opacity' ? 'range' : 'number';
    input.min = String(this.target.min);
    input.max = String(this.target.max);
    input.step = String(this.target.step);
    input.value = String(this.target.value);
    input.setAttribute('aria-label', `Edit ${this.target.property}`);
    const value = document.createElement('output');
    value.textContent = String(this.target.value);
    input.addEventListener('input', () => {
      value.textContent = input.value;
      view.dispatch({ changes: { from: this.target.from, to: this.target.to, insert: input.value } });
    });
    label.append(input, value);
    return label;
  }

  ignoreEvent(): boolean {
    return false;
  }
}

function controlDecorations(view: EditorView): DecorationSet {
  const targets = recipeProjectionTargets(view.state.doc.toString());
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
  stageId: string;
  images: readonly ImageData[];
  artifactCount: number;
  version: number;
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
    return this.preview.stageId === other.preview.stageId && this.preview.version === other.preview.version;
  }

  toDOM(): HTMLElement {
    const panel = document.createElement('section');
    panel.className = 'binscript-stage-preview';
    const heading = document.createElement('div');
    heading.className = 'binscript-stage-preview-heading';
    const title = document.createElement('strong');
    title.textContent = this.preview.stageId;
    const count = document.createElement('code');
    count.textContent = `${this.preview.artifactCount} artifact${this.preview.artifactCount === 1 ? '' : 's'}`;
    heading.append(title, count);
    const strip = document.createElement('div');
    strip.className = 'binscript-preview-strip';
    for (const [index, image] of this.preview.images.entries()) {
      const canvas = document.createElement('canvas');
      canvas.className = 'binscript-preview-canvas';
      canvas.setAttribute('aria-label', `${this.preview.stageId} preview ${index + 1}`);
      drawPreview(canvas, image);
      strip.append(canvas);
    }
    if (this.preview.artifactCount > this.preview.images.length) {
      const remainder = document.createElement('span');
      remainder.className = 'binscript-preview-remainder';
      remainder.textContent = `+${this.preview.artifactCount - this.preview.images.length}`;
      strip.append(remainder);
    }
    panel.append(heading, strip);
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
            Decoration.widget({ widget: new StagePreviewWidget(preview), block: true, side: 1 }).range(preview.at),
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
  status: HTMLElement;
  runButton: HTMLButtonElement;
  resetButton: HTMLButtonElement;
  initialSource?: string;
}

export interface RecipeNotebook {
  view: EditorView;
  run: () => Promise<void>;
  reset: () => void;
}

export function createRecipeNotebook({
  parent,
  status,
  runButton,
  resetButton,
  initialSource = STARTER_SOURCE,
}: RecipeNotebookOptions): RecipeNotebook {
  let evaluationVersion = 0;
  let timer: number | undefined;

  const run = async (): Promise<void> => {
    const source = view.state.doc.toString();
    const version = ++evaluationVersion;
    status.textContent = 'compiling...';
    try {
      const input: unknown = JSON.parse(source);
      const result = await compileRecipe(input, { maximumArtifacts: 2_000 });
      if (version !== evaluationVersion || source !== view.state.doc.toString()) return;
      const targets = recipeProjectionTargets(source).filter(
        (target): target is Extract<ProjectionTarget, { kind: 'stage' }> => target.kind === 'stage',
      );
      const previews = targets.map((target) => {
        const artifacts = result.stages.get(target.stageId) ?? [];
        return {
          at: target.at,
          stageId: target.stageId,
          images: artifacts.slice(0, 8).map((artifact: RecipeArtifact) => artifact.image),
          artifactCount: artifacts.length,
          version,
        };
      });
      view.dispatch({ effects: setStagePreviews.of(previews) }, setDiagnostics(view.state, []));
      const totalArtifacts = [...result.stages.values()].reduce((total, artifacts) => total + artifacts.length, 0);
      status.textContent = `${totalArtifacts} artifacts · ${previews.length} live stages`;
    } catch (error: unknown) {
      if (version !== evaluationVersion) return;
      view.dispatch({ effects: setStagePreviews.of([]) }, setDiagnostics(view.state, [diagnosticFor(error, view.state.doc.length)]));
      status.textContent = errorSummary(error);
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
      basicSetup,
      json(),
      lintGutter(),
      controlPlugin,
      previewField,
      EditorView.lineWrapping,
      EditorView.updateListener.of((update) => {
        if (update.docChanged) scheduleRun();
      }),
      keymap.of([
        {
          key: 'Mod-Enter',
          run: () => {
            void run();
            return true;
          },
        },
      ]),
      EditorView.theme({
        '&': { fontSize: '14px' },
        '.cm-content': { fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace', padding: '14px 0 80px' },
        '.cm-gutters': { backgroundColor: '#f6f6f1', borderRight: '1px solid #111' },
        '.cm-activeLine, .cm-activeLineGutter': { backgroundColor: '#e9ffff' },
        '&.cm-focused': { outline: 'none' },
      }),
    ],
  });

  const reset = (): void => {
    view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: STARTER_SOURCE } });
    void run();
  };

  runButton.addEventListener('click', () => void run());
  resetButton.addEventListener('click', reset);
  void run();

  return { view, run, reset };
}
