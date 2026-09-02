import assert from 'node:assert/strict';
import test from 'node:test';

import { highlightBinScript } from '../../scripts/binscript-highlighter.js';

test('BinScript TextMate grammar highlights the language tokens', () => {
  const html = highlightBinScript(`import "binblock/basic"
size := 64
fill(#ff0000).opacity(50%) // red`);

  assert.match(html, /#D60000">import</);
  assert.match(html, /#006400"> "binblock\/basic"/);
  assert.match(html, /#005F73">size/);
  assert.match(html, /#A00072">#ff0000/);
  assert.match(html, /#0000CC">opacity/);
  assert.match(html, /#7A3E00">50%/);
  assert.match(html, /font-style:italic"> \/\/ red/);
});

test('BinScript highlighting preserves the complete source text', () => {
  const source = `field := rg(black 64%, transparent-black, center: [31.5, -2], radius: 32, easing: 'reference')`;
  const html = highlightBinScript(source);
  const text = html
    .replace(/<[^>]+>/g, '')
    .replaceAll('&lt;', '<')
    .replaceAll('&gt;', '>')
    .replaceAll('&amp;', '&');

  assert.equal(text, source);
});
