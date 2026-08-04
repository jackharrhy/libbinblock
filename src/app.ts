import './style.css';
import { createRecipeNotebook } from './binscript-notebook.js';

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
      <div class="notebook-brand"><strong>BinScript</strong><span>code is the interface</span></div>
      <div class="notebook-actions">
        <output id="notebook-status">starting...</output>
        <button id="reset-notebook" type="button">reset</button>
        <button id="run-notebook" type="button">run <kbd>⌘↵</kbd></button>
      </div>
    </header>
    <div id="binscript-editor" aria-label="BinScript recipe editor"></div>
  </section>`;

createRecipeNotebook({
  parent: requiredElement('#binscript-editor', HTMLElement),
  status: requiredElement('#notebook-status', HTMLOutputElement),
  runButton: requiredElement('#run-notebook', HTMLButtonElement),
  resetButton: requiredElement('#reset-notebook', HTMLButtonElement),
});
