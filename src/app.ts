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
      <span class="notebook-brand">BinScript</span>
    </header>
    <div id="binscript-editor" aria-label="BinScript recipe editor"></div>
  </section>`;

createRecipeNotebook({
  parent: requiredElement('#binscript-editor', HTMLElement),
});
