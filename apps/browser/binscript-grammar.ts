import type { LanguageRegistration } from 'shiki/types';

const identifier = '[A-Za-z_][A-Za-z0-9_-]*';

export const binScriptConstants = [
  'transparent-black',
  'transparent-white',
  'transparent',
  'black',
  'white',
  'red',
  'blue',
  'true',
  'false',
] as const;

const constantPattern = `\\b(?:${binScriptConstants.join('|')})\\b`;

export const binScriptGrammar: LanguageRegistration = {
  name: 'binscript',
  displayName: 'BinScript',
  scopeName: 'source.binscript',
  aliases: ['binblock', 'bb'],
  patterns: [
    { include: '#comments' },
    { include: '#strings' },
    { name: 'constant.other.color.binscript', match: '#(?:[0-9A-Fa-f]{8}|[0-9A-Fa-f]{6})(?![0-9A-Fa-f])' },
    {
      name: 'constant.numeric.binscript',
      match: '(?<![A-Za-z0-9_-])-?(?:(?:[0-9]+(?:\\.[0-9]*)?)|(?:\\.[0-9]+))(?:[eE][+-]?[0-9]+)?(?:%|deg(?![A-Za-z0-9_-]))?',
    },
    { name: 'keyword.control.import.binscript', match: '\\bimport\\b' },
    {
      match: `^([ \\t]*)(${identifier})([ \\t]*)(:=)`,
      captures: {
        2: { name: 'variable.other.definition.binscript' },
        4: { name: 'keyword.operator.assignment.binscript' },
      },
    },
    { name: 'variable.parameter.binscript', match: `\\b${identifier}(?=\\s*:(?!=))` },
    { name: 'constant.language.binscript', match: constantPattern },
    {
      match: `(\\.)(${identifier})(?=\\s*\\()`,
      captures: {
        1: { name: 'punctuation.accessor.binscript' },
        2: { name: 'entity.name.function.member.binscript' },
      },
    },
    { name: 'entity.name.function.binscript', match: `\\b${identifier}(?=\\s*\\()` },
    { name: 'keyword.operator.assignment.binscript', match: ':=' },
    { name: 'punctuation.separator.binscript', match: '[,:;.]' },
    { name: 'punctuation.bracket.binscript', match: '[\\[\\]()]' },
    { name: 'variable.other.binscript', match: `\\b${identifier}\\b` },
  ],
  repository: {
    comments: {
      patterns: [
        { name: 'comment.line.double-slash.binscript', begin: '//', end: '$' },
        { name: 'comment.block.binscript', begin: '/\\*', end: '\\*/' },
      ],
    },
    strings: {
      patterns: [
        {
          name: 'string.quoted.double.binscript',
          begin: '"',
          end: '"',
          patterns: [{ name: 'constant.character.escape.binscript', match: '\\\\.' }],
        },
        {
          name: 'string.quoted.single.binscript',
          begin: "'",
          end: "'",
          patterns: [{ name: 'constant.character.escape.binscript', match: '\\\\.' }],
        },
      ],
    },
  },
};
