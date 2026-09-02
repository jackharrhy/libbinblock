import { createHighlighterCoreSync } from 'shiki/core';
import { createJavaScriptRegexEngine } from 'shiki/engine/javascript';
import type { ThemeRegistration } from 'shiki/types';

import { binScriptGrammar } from '../apps/browser/binscript-grammar.js';

const binBlockTheme: ThemeRegistration = {
  name: 'binblock-light',
  type: 'light',
  colors: {
    'editor.background': '#eaffff',
    'editor.foreground': '#171717',
  },
  settings: [
    { settings: { foreground: '#171717', background: '#eaffff' } },
    { scope: ['comment'], settings: { foreground: '#626262', fontStyle: 'italic' } },
    { scope: ['keyword'], settings: { foreground: '#d60000' } },
    { scope: ['string'], settings: { foreground: '#006400' } },
    { scope: ['constant.numeric'], settings: { foreground: '#7a3e00' } },
    { scope: ['constant.other.color', 'constant.language'], settings: { foreground: '#a00072' } },
    { scope: ['entity.name.function'], settings: { foreground: '#0000cc' } },
    { scope: ['variable.other.definition'], settings: { foreground: '#005f73' } },
    { scope: ['variable.parameter'], settings: { foreground: '#7a3e00' } },
    { scope: ['keyword.operator'], settings: { foreground: '#d60000' } },
    { scope: ['punctuation'], settings: { foreground: '#626262' } },
  ],
};

const highlighter = createHighlighterCoreSync({
  langs: [binScriptGrammar],
  themes: [binBlockTheme],
  engine: createJavaScriptRegexEngine(),
});

export function highlightBinScript(source: string): string {
  return highlighter.codeToHtml(source, { lang: 'binscript', theme: 'binblock-light' });
}
