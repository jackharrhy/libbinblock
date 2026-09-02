import { HighlightStyle, StreamLanguage, syntaxHighlighting, type StreamParser, type StringStream } from '@codemirror/language';
import { tags } from '@lezer/highlight';

import { binScriptConstants } from './binscript-grammar.js';

interface BinScriptStreamState {
  blockComment: boolean;
  quote?: string;
}

const constantNames = new Set<string>(binScriptConstants);
const identifierExpression = /^[A-Za-z_][A-Za-z0-9_-]*/;
const numberExpression = /^-?(?:(?:[0-9]+(?:\.[0-9]*)?)|(?:\.[0-9]+))(?:[eE][+-]?[0-9]+)?(?:%|deg(?![A-Za-z0-9_-]))?/;
const colorExpression = /^#(?:[0-9A-Fa-f]{8}|[0-9A-Fa-f]{6})(?![0-9A-Fa-f])/;

function readString(stream: StringStream, state: BinScriptStreamState, quote: string): string {
  let escaped = false;
  while (!stream.eol()) {
    const character = stream.next();
    if (escaped) escaped = false;
    else if (character === '\\') escaped = true;
    else if (character === quote) {
      state.quote = undefined;
      break;
    }
  }
  return 'string';
}

const binScriptStreamParser: StreamParser<BinScriptStreamState> = {
  name: 'binscript',
  startState: () => ({ blockComment: false }),
  token(stream, state) {
    if (state.blockComment) {
      if (stream.skipTo('*/')) {
        stream.pos += 2;
        state.blockComment = false;
      } else stream.skipToEnd();
      return 'comment';
    }
    if (state.quote) return readString(stream, state, state.quote);
    if (stream.eatSpace()) return null;
    if (stream.match('//')) {
      stream.skipToEnd();
      return 'comment';
    }
    if (stream.match('/*')) {
      state.blockComment = true;
      if (stream.skipTo('*/')) {
        stream.pos += 2;
        state.blockComment = false;
      } else stream.skipToEnd();
      return 'comment';
    }
    const quote = stream.peek();
    if (quote === '"' || quote === "'") {
      stream.next();
      state.quote = quote;
      return readString(stream, state, quote);
    }
    if (stream.match(colorExpression)) return 'color';
    if (stream.match(numberExpression)) return 'number';
    if (stream.match(':=')) return 'operator';
    const identifierMatch = stream.match(identifierExpression);
    if (identifierMatch && typeof identifierMatch !== 'boolean') {
      const name = identifierMatch[0];
      const before = stream.string.slice(0, stream.start);
      const after = stream.string.slice(stream.pos);
      if (name === 'import') return 'keyword';
      if (/^\s*\(/.test(after)) return before.trimEnd().endsWith('.') ? 'propertyName' : 'variableName.function';
      if (/^\s*:(?!=)/.test(after)) return 'propertyName';
      if (/^\s*:=/.test(after)) return 'variableName.definition';
      if (constantNames.has(name)) return name === 'true' || name === 'false' ? 'bool' : 'color';
      return 'variableName';
    }
    const character = stream.next();
    if (character && '[]()'.includes(character)) return 'bracket';
    if (character && ',:;.'.includes(character)) return 'punctuation';
    return 'invalid';
  },
  languageData: {
    commentTokens: { line: '//', block: { open: '/*', close: '*/' } },
    closeBrackets: { brackets: ['(', '[', '"', "'"] },
  },
};

export const binScriptEditorExtensions = [
  StreamLanguage.define(binScriptStreamParser),
  syntaxHighlighting(
    HighlightStyle.define([
      { tag: tags.comment, color: '#626262', fontStyle: 'italic' },
      { tag: tags.keyword, color: '#d60000' },
      { tag: tags.string, color: '#006400' },
      { tag: tags.number, color: '#7a3e00' },
      { tag: [tags.color, tags.bool], color: '#a00072' },
      { tag: tags.function(tags.variableName), color: '#0000cc' },
      { tag: tags.propertyName, color: '#0000cc' },
      { tag: tags.definition(tags.variableName), color: '#005f73' },
      { tag: tags.operator, color: '#d60000' },
      { tag: tags.punctuation, color: '#626262' },
      { tag: tags.invalid, color: '#d60000', textDecoration: 'underline wavy' },
    ]),
  ),
] as const;
