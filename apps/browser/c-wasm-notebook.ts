import { minimalSetup } from 'codemirror';
import { setDiagnostics, type Diagnostic } from '@codemirror/lint';
import { EditorView } from '@codemirror/view';
import {
  createBinBlockRuntime,
  type BinBlockDiagnostic,
  type BinBlockImage,
  type BinBlockParameter,
  type BinBlockRuntime,
  type BinBlockWasmFactory,
} from '../../bindings/javascript/binblock.js';
import { BinBlockWebGl2Backend } from '../../bindings/javascript/webgl2-backend.js';
import { binScriptEditorExtensions } from './binscript-language.js';
import { GENERATED_SET_SOURCE } from './generated-set.js';

export const BINBLOCK_NOTEBOOK_STORAGE_KEY = 'binblock:notebook-source:v2';

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

export interface CWasmNotebookOptions {
  parent: HTMLElement;
  preview: HTMLElement;
  status: HTMLElement;
  parameters: HTMLElement;
  initialSource?: string;
  generatedSetButton?: HTMLButtonElement;
  renderAllButton?: HTMLButtonElement;
  exportButton?: HTMLButtonElement;
  previewStart?: HTMLInputElement;
}

export interface CWasmNotebook {
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

export async function createCWasmNotebook({
  parent,
  preview,
  status,
  parameters,
  initialSource = C_WASM_STARTER_SOURCE,
  generatedSetButton,
  renderAllButton,
  exportButton,
  previewStart,
}: CWasmNotebookOptions): Promise<CWasmNotebook> {
  const moduleUrl = new URL('/binblock.mjs', window.location.href).href;
  const { default: createBinBlockModule } = (await import(/* @vite-ignore */ moduleUrl)) as {
    default: BinBlockWasmFactory;
  };
  const runtime = await createBinBlockRuntime(createBinBlockModule);
  const webgl = new BinBlockWebGl2Backend();
  const events = new AbortController();
  let timer: number | undefined;
  let version = 0;
  const sampleSize = 8;
  const batchSize = 24;
  const renderAllLimit = 10_000n;

  const frame = (): Promise<void> => new Promise((resolve) => window.requestAnimationFrame(() => resolve()));

  const updateRenderAllButton = (total: bigint, expandable: boolean, busy = false): void => {
    if (!renderAllButton) return;
    renderAllButton.disabled = busy || !expandable || total > renderAllLimit;
    renderAllButton.textContent = busy ? 'Rendering...' : 'Render all';
    if (total > renderAllLimit)
      renderAllButton.title = `The editor can render all collections up to ${renderAllLimit.toLocaleString()} items.`;
    else renderAllButton.removeAttribute('title');
  };

  const renderCompiledOutputs = async (runVersion: number, renderAll = false): Promise<void> => {
    const outputs = runtime.outputs();
    const total = outputs.reduce((sum, output) => sum + output.cardinality, 0n);
    const expandable = outputs.some((output) => output.cardinality > BigInt(sampleSize));
    const startedAt = performance.now();
    let rendered = 0;
    let gpuChecked = 0;
    let gpuValidated = 0;
    let gpuMaxError = 0;
    const sampleFragment = document.createDocumentFragment();
    updateRenderAllButton(total, expandable, renderAll);
    if (renderAll) {
      previewStart?.setAttribute('value', '0');
      if (previewStart) previewStart.value = '0';
      preview.replaceChildren();
    }

    for (const output of outputs) {
      const requestedStart = renderAll ? 0n : BigInt(Math.max(0, Math.trunc(Number(previewStart?.value ?? 0) || 0)));
      const start = requestedStart < output.cardinality ? requestedStart : output.cardinality;
      const remaining = output.cardinality - start;
      const wanted = renderAll ? remaining : remaining < BigInt(sampleSize) ? remaining : BigInt(sampleSize);
      let offset = 0n;
      while (offset < wanted) {
        const count = Number(wanted - offset < BigInt(batchSize) ? wanted - offset : BigInt(batchSize));
        const artifactStart = start + offset;
        const artifacts = runtime.artifacts(output.index, artifactStart, count);
        if (runVersion !== version) return;

        const fragment = document.createDocumentFragment();
        for (let index = 0; index < artifacts.length; index += 1) {
          const cpuImage = await runtime.render(output.index, artifactStart + BigInt(index));
          if (runVersion !== version) return;
          let image = cpuImage;
          if (gpuChecked < sampleSize) {
            gpuChecked += 1;
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
          }
          fragment.append(drawImage(image, artifacts[index].key));
          rendered += 1;
        }
        if (renderAll) preview.append(fragment);
        else sampleFragment.append(fragment);
        offset += BigInt(count);

        if (renderAll) {
          status.textContent = `Rendering ${rendered.toLocaleString()} of ${total.toLocaleString()} previews...`;
          await frame();
          if (runVersion !== version) return;
        }
      }
    }
    if (runVersion !== version) return;
    if (!renderAll) preview.replaceChildren(sampleFragment);
    const duration = ((performance.now() - startedAt) / 1000).toFixed(1);
    const scope = renderAll
      ? `rendered ${rendered.toLocaleString()} in ${duration}s`
      : `showing ${rendered.toLocaleString()} of ${total.toLocaleString()}`;
    status.textContent = `${outputs.length} outputs | ${scope} | WebGL2 matched ${gpuValidated} of ${gpuChecked} checked previews (largest channel difference: ${gpuMaxError}) | compiler generation ${runtime.generation}`;
    updateRenderAllButton(total, expandable);
  };

  const requestRender = (renderAll: boolean): void => {
    const runVersion = ++version;
    runtime.supersede();
    void renderCompiledOutputs(runVersion, renderAll).catch((error: unknown) => {
      if (runVersion !== version || (error instanceof DOMException && error.name === 'AbortError')) return;
      status.textContent = error instanceof Error ? error.message : String(error);
      const outputs = runtime.outputs();
      updateRenderAllButton(
        outputs.reduce((sum, output) => sum + output.cardinality, 0n),
        outputs.some((output) => output.cardinality > BigInt(sampleSize)),
      );
    });
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
        status.textContent = `${diagnostics.length} compiler messages. Fix the errors to render a preview.`;
        return;
      }
      const controls = runtime.parameters().map((parameter) => parameterControl(runtime, parameter, () => requestRender(false)));
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
      ...binScriptEditorExtensions,
      EditorView.lineWrapping,
      EditorView.updateListener.of((update) => {
        if (update.docChanged) {
          localStorage.setItem(BINBLOCK_NOTEBOOK_STORAGE_KEY, update.state.doc.toString());
          scheduleRun();
        }
        if (update.selectionSet) {
          const trace = runtime.traceAt(update.state.selection.main.head);
          if (trace) status.dataset.trace = `type ${trace.type} | ${trace.from}:${trace.to}`;
          else delete status.dataset.trace;
        }
      }),
      EditorView.theme({
        '&': { backgroundColor: 'var(--surface)', color: 'var(--ink)', fontSize: '16px' },
        '.cm-content': { fontFamily: 'var(--font-mono)', padding: '18px 16px 96px' },
        '.cm-gutters': { backgroundColor: 'var(--surface-subtle)', color: 'var(--muted)', borderRight: '1px solid var(--line)' },
        '&.cm-focused': { outline: 'none' },
      }),
    ],
  });
  generatedSetButton?.addEventListener(
    'click',
    () => {
      status.textContent = 'Loading the generated palette and layer matrix...';
      view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: GENERATED_SET_SOURCE } });
    },
    { signal: events.signal },
  );
  renderAllButton?.addEventListener('click', () => requestRender(true), { signal: events.signal });
  exportButton?.addEventListener(
    'click',
    () => {
      const url = URL.createObjectURL(new Blob([view.state.doc.toString()], { type: 'text/plain;charset=utf-8' }));
      const link = document.createElement('a');
      link.href = url;
      link.download = 'program.binscript';
      link.click();
      URL.revokeObjectURL(url);
    },
    { signal: events.signal },
  );
  previewStart?.addEventListener('input', () => requestRender(false), { signal: events.signal });
  await run();
  return {
    view,
    runtime,
    dispose() {
      if (timer !== undefined) window.clearTimeout(timer);
      events.abort();
      version += 1;
      runtime.supersede();
      runtime.dispose();
      webgl.dispose();
      view.destroy();
    },
  };
}
