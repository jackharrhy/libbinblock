import './style.css';
import { createCWasNotebook } from './c-wasm-notebook.js';

type ElementConstructor<T extends Element> = new () => T;

function requiredElement<T extends Element>(selector: string, constructor: ElementConstructor<T>, root: ParentNode = document): T {
  const element = root.querySelector(selector);
  if (!(element instanceof constructor)) throw new Error(`Required element not found or has the wrong type: ${selector}`);
  return element;
}

const app = requiredElement('#app', HTMLElement);
app.innerHTML = `
  <section class="notebook-app" aria-label="BinScript notebook">
    <header class="notebook-bar">
      <span class="notebook-brand">BinScript</span>
      <button id="load-reference" type="button">Open 4,312-item reference program</button>
      <label class="notebook-preview-start">Preview start <input id="preview-start" type="number" min="0" step="1" value="0"></label>
      <button id="export-source" type="button">Export source</button>
    </header>
    <div class="notebook-runtime-status" id="runtime-status">Loading portable C/Wasm compiler…</div>
    <div class="notebook-parameters" id="runtime-parameters" aria-label="BinScript parameters"></div>
    <div class="notebook-workspace">
      <div id="binscript-editor" aria-label="BinScript recipe editor"></div>
      <aside class="notebook-preview" id="runtime-preview" aria-label="Generated previews"></aside>
    </div>
  </section>`;

await createCWasNotebook({
  parent: requiredElement('#binscript-editor', HTMLElement),
  preview: requiredElement('#runtime-preview', HTMLElement),
  status: requiredElement('#runtime-status', HTMLElement),
  parameters: requiredElement('#runtime-parameters', HTMLElement),
  referenceButton: requiredElement('#load-reference', HTMLButtonElement),
  exportButton: requiredElement('#export-source', HTMLButtonElement),
  previewStart: requiredElement('#preview-start', HTMLInputElement),
  initialSource: localStorage.getItem('binblock:notebook-source') ?? undefined,
});
