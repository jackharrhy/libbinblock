import { minimalSetup } from 'codemirror';
import { setDiagnostics, type Diagnostic } from '@codemirror/lint';
import { EditorView } from '@codemirror/view';
import createBinBlockModule from './binblock.mjs';
import {
  createBinBlockRuntime,
  type BinBlockDiagnostic,
  type BinBlockImage,
  type BinBlockParameter,
  type BinBlockRuntime,
} from '../bindings/javascript/binblock.js';
import { BinBlockWebGl2Backend } from '../bindings/javascript/webgl2-backend.js';
import { ReferenceAssetHost } from '../bindings/javascript/reference-assets.js';

export const C_WASM_STARTER_SOURCE = `import "binblock/basic"

size := 64
colors := palette(
  black: #000000,
  gray: #808080,
  white: #ffffff,
  blue: #0000ff,
  cyan: #00ffff,
  green: #00ff00,
  red: #ff0000,
  magenta: #ff00ff,
)
blocks := colors.map(fill).size(size)
blocks

fade := lg(180deg, white, transparent-white).size(size)
blocks.mask(fade)
`;

export interface CWasNotebookOptions {
  parent: HTMLElement;
  preview: HTMLElement;
  status: HTMLElement;
  parameters: HTMLElement;
  initialSource?: string;
  referenceButton?: HTMLButtonElement;
  exportButton?: HTMLButtonElement;
  previewStart?: HTMLInputElement;
}

export interface CWasNotebook {
  view: EditorView;
  runtime: BinBlockRuntime;
  dispose(): void;
}

function editorDiagnostic(diagnostic: BinBlockDiagnostic, documentLength: number): Diagnostic {
  const from = Math.min(diagnostic.from, Math.max(0, documentLength - 1));
  return {
    from,
    to: Math.min(Math.max(diagnostic.to, from + 1), documentLength),
    severity: diagnostic.severity === 2 ? 'error' : diagnostic.severity === 1 ? 'warning' : 'info',
    message: diagnostic.message,
  };
}

function drawImage(image: BinBlockImage, label: string): HTMLElement {
  const figure = document.createElement('figure');
  const canvas = document.createElement('canvas');
  const caption = document.createElement('figcaption');
  const context = canvas.getContext('2d');
  if (!context) throw new Error('2D canvas is unavailable.');
  canvas.width = image.width;
  canvas.height = image.height;
  canvas.setAttribute('aria-label', `${label}, ${image.width} by ${image.height}`);
  context.putImageData(new ImageData(Uint8ClampedArray.from(image.pixels), image.width, image.height), 0, 0);
  caption.textContent = label;
  figure.append(canvas, caption);
  return figure;
}

function parameterControl(runtime: BinBlockRuntime, parameter: BinBlockParameter, rerender: () => void): HTMLElement {
  const label = document.createElement('label');
  const name = document.createElement('span');
  name.textContent = parameter.name;
  label.append(name);
  if (parameter.type === 5) {
    const input = document.createElement('input');
    input.type = 'color';
    input.value = '#000000';
    input.addEventListener('input', () => {
      runtime.setParameter(parameter.index, parameter.type, `${input.value}ff`);
      rerender();
    });
    label.append(input);
  } else if (parameter.type === 2 || parameter.type === 3 || parameter.type === 8 || parameter.type === 9) {
    const input = document.createElement('input');
    input.type = 'number';
    input.value = parameter.type === 2 ? '64' : '0';
    input.step = parameter.type === 2 ? '1' : 'any';
    input.addEventListener('input', () => {
      const number = Number(input.value);
      if (!Number.isFinite(number) || input.value === '') return;
      runtime.setParameter(parameter.index, parameter.type, parameter.type === 2 ? BigInt(Math.trunc(number)) : number);
      rerender();
    });
    label.append(input);
  }
  return label;
}

export async function createCWasNotebook({
  parent,
  preview,
  status,
  parameters,
  initialSource = C_WASM_STARTER_SOURCE,
  referenceButton,
  exportButton,
  previewStart,
}: CWasNotebookOptions): Promise<CWasNotebook> {
  const runtime = await createBinBlockRuntime(createBinBlockModule);
  const webgl = new BinBlockWebGl2Backend();
  let referenceAssets: ReferenceAssetHost | undefined;
  let timer: number | undefined;
  let version = 0;

  const renderCompiledOutputs = async (runVersion: number): Promise<void> => {
    const fragment = document.createDocumentFragment();
    let rendered = 0;
    let gpuValidated = 0;
    let gpuMaxError = 0;
    for (const output of runtime.outputs()) {
      const requestedStart = BigInt(Math.max(0, Math.trunc(Number(previewStart?.value ?? 0) || 0)));
      const start = requestedStart < output.cardinality ? requestedStart : output.cardinality;
      const remaining = output.cardinality - start;
      const count = Number(remaining < 8n ? remaining : 8n);
      const artifacts = runtime.artifacts(output.index, start, count);
      for (let index = 0; index < artifacts.length; index += 1) {
        if (referenceAssets) await referenceAssets.hydrateGraph(artifacts[index].image);
        if (runVersion !== version) return;
        const cpuImage = await runtime.render(output.index, start + BigInt(index));
        if (runVersion !== version) return;
        let image = cpuImage;
        try {
          const gpuImage = webgl.render(runtime, artifacts[index].image);
          if (gpuImage && gpuImage.width === cpuImage.width && gpuImage.height === cpuImage.height) {
            let maxError = 0;
            for (let pixel = 0; pixel < cpuImage.pixels.length; pixel += 1)
              maxError = Math.max(maxError, Math.abs(cpuImage.pixels[pixel] - gpuImage.pixels[pixel]));
            if (maxError <= gpuImage.maxChannelError) {
              image = gpuImage;
              gpuValidated += 1;
              gpuMaxError = Math.max(gpuMaxError, maxError);
            }
          }
        } catch {
          // Shader compilation and context loss are explicit CPU-fallback paths.
        }
        fragment.append(drawImage(image, artifacts[index].key));
        rendered += 1;
      }
    }
    if (runVersion !== version) return;
    preview.replaceChildren(fragment);
    status.textContent = `${runtime.outputs().length} outputs · ${rendered} bounded previews · ${gpuValidated} WebGL2 validated (max error ${gpuMaxError}) · C/Wasm generation ${runtime.generation}`;
  };

  const run = async (): Promise<void> => {
    const runVersion = ++version;
    runtime.supersede();
    const source = view.state.doc.toString();
    try {
      const diagnostics = runtime.compile(source);
      if (runVersion !== version || source !== view.state.doc.toString()) return;
      view.dispatch(
        setDiagnostics(
          view.state,
          diagnostics.map((diagnostic) => editorDiagnostic(diagnostic, view.state.doc.length)),
        ),
      );
      if (diagnostics.some((diagnostic) => diagnostic.severity === 2)) {
        preview.replaceChildren();
        parameters.replaceChildren();
        status.textContent = `${diagnostics.length} compiler diagnostics`;
        return;
      }
      const controls = runtime
        .parameters()
        .map((parameter) => parameterControl(runtime, parameter, () => void renderCompiledOutputs(++version)));
      parameters.replaceChildren(...controls);
      await renderCompiledOutputs(runVersion);
    } catch (error) {
      if (runVersion !== version || (error instanceof DOMException && error.name === 'AbortError')) return;
      status.textContent = error instanceof Error ? error.message : String(error);
    }
  };

  const scheduleRun = (): void => {
    if (timer !== undefined) window.clearTimeout(timer);
    timer = window.setTimeout(() => void run(), 180);
  };

  const view = new EditorView({
    parent,
    doc: initialSource,
    extensions: [
      minimalSetup,
      EditorView.lineWrapping,
      EditorView.updateListener.of((update) => {
        if (update.docChanged) {
          localStorage.setItem('binblock:notebook-source', update.state.doc.toString());
          scheduleRun();
        }
        if (update.selectionSet) {
          const trace = runtime.traceAt(update.state.selection.main.head);
          if (trace) status.dataset.trace = `type ${trace.type} · ${trace.from}:${trace.to}`;
          else delete status.dataset.trace;
        }
      }),
      EditorView.theme({
        '&': { fontSize: '14px' },
        '.cm-content': { fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace', padding: '18px 16px 96px' },
        '&.cm-focused': { outline: 'none' },
      }),
    ],
  });
  referenceButton?.addEventListener('click', async () => {
    try {
      status.textContent = 'Loading the 4,312-item reference manifest (images remain on demand)…';
      referenceAssets ??= await ReferenceAssetHost.load(runtime);
      view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: referenceAssets.source } });
    } catch (error) {
      status.textContent = error instanceof Error ? error.message : String(error);
    }
  });
  exportButton?.addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([view.state.doc.toString()], { type: 'text/plain;charset=utf-8' }));
    const link = document.createElement('a');
    link.href = url;
    link.download = 'program.binscript';
    link.click();
    URL.revokeObjectURL(url);
  });
  previewStart?.addEventListener('input', () => void renderCompiledOutputs(++version));
  if (initialSource.includes('import "binblock/reference-set"')) {
    status.textContent = 'Restoring reference metadata (images remain on demand)…';
    referenceAssets = await ReferenceAssetHost.load(runtime);
  }
  await run();
  return {
    view,
    runtime,
    dispose() {
      if (timer !== undefined) window.clearTimeout(timer);
      runtime.dispose();
      webgl.dispose();
      view.destroy();
    },
  };
}
