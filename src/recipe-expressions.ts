import type { Expression } from './recipe-schema.js';

export type ExpressionContext = {
  variables?: Readonly<Record<string, unknown>>;
  properties?: Readonly<Record<string, unknown>>;
};

export type ExpressionResult = unknown;

const OPERATORS: ReadonlySet<string> = new Set([
  'literal',
  'var',
  'get',
  'coalesce',
  'concat',
  'min',
  'max',
  '==',
  '!=',
  '<',
  '<=',
  '>',
  '>=',
  '+',
  '-',
  '*',
  '/',
  'clamp',
  'to-string',
  'pad',
  'case',
  'match',
  'let',
]);

export function isExpression(value: unknown): value is Expression {
  return Array.isArray(value) && typeof value[0] === 'string' && OPERATORS.has(value[0]);
}

function finiteNumber(value: unknown, operator: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) throw new Error(`${operator} expects finite numbers.`);
  return value;
}

function evaluateMany(values: readonly unknown[], context: ExpressionContext): ExpressionResult[] {
  return values.map((value) => evaluateExpressionValue(value, context));
}

export function evaluateExpression(expression: Expression, context: ExpressionContext = {}): ExpressionResult {
  const [operator, ...args] = expression as readonly [string, ...unknown[]];
  if (operator === 'literal') return args[0];
  if (operator === 'var') {
    const name = args[0] as string;
    if (!Object.hasOwn(context.variables ?? {}, name)) throw new Error(`Unknown variable: ${name}`);
    return context.variables?.[name];
  }
  if (operator === 'get') {
    const object = args.length === 1 ? context.properties : evaluateExpressionValue(args[1], context);
    if (object === null || typeof object !== 'object') throw new Error(`Cannot get ${args[0]} from a non-object value.`);
    return (object as Record<string, unknown>)[args[0] as string];
  }
  if (operator === 'coalesce') {
    for (const value of args) {
      const result = evaluateExpressionValue(value, context);
      if (result !== null && result !== undefined) return result;
    }
    return null;
  }
  if (operator === 'concat') return evaluateMany(args, context).map(String).join('');
  if (operator === 'to-string') return String(evaluateExpressionValue(args[0], context));
  if (operator === 'pad') {
    const value = String(evaluateExpressionValue(args[0], context));
    const length = finiteNumber(evaluateExpressionValue(args[1], context), operator);
    const character = args[2] === undefined ? '0' : String(evaluateExpressionValue(args[2], context));
    if (character.length !== 1) throw new Error('pad expects a single padding character.');
    return value.padStart(length, character);
  }
  if (operator === 'case') {
    for (let index = 0; index < args.length - 1; index += 2) {
      if (evaluateExpressionValue(args[index], context)) return evaluateExpressionValue(args[index + 1], context);
    }
    return evaluateExpressionValue(args.at(-1), context);
  }
  if (operator === 'match') {
    const input = evaluateExpressionValue(args[0], context);
    for (let index = 1; index < args.length - 1; index += 2) {
      const label = evaluateExpressionValue(args[index], context);
      if (Array.isArray(label) ? label.includes(input) : label === input) return evaluateExpressionValue(args[index + 1], context);
    }
    return evaluateExpressionValue(args.at(-1), context);
  }
  if (operator === 'let') {
    const variables: Record<string, unknown> = { ...context.variables };
    for (let index = 0; index < args.length - 1; index += 2) {
      variables[args[index] as string] = evaluateExpressionValue(args[index + 1], { ...context, variables });
    }
    return evaluateExpressionValue(args.at(-1), { ...context, variables });
  }

  const values = evaluateMany(args, context);
  if (operator === '==') return values[0] === values[1];
  if (operator === '!=') return values[0] !== values[1];
  if (operator === '<') return (values[0] as string) < (values[1] as string);
  if (operator === '<=') return (values[0] as string) <= (values[1] as string);
  if (operator === '>') return (values[0] as string) > (values[1] as string);
  if (operator === '>=') return (values[0] as string) >= (values[1] as string);
  if (operator === '+') return finiteNumber(values[0], operator) + finiteNumber(values[1], operator);
  if (operator === '-') return finiteNumber(values[0], operator) - finiteNumber(values[1], operator);
  if (operator === '*') return finiteNumber(values[0], operator) * finiteNumber(values[1], operator);
  if (operator === '/') {
    const divisor = finiteNumber(values[1], operator);
    if (divisor === 0) throw new Error('Division by zero.');
    return finiteNumber(values[0], operator) / divisor;
  }
  if (operator === 'min') return Math.min(...values.map((value) => finiteNumber(value, operator)));
  if (operator === 'max') return Math.max(...values.map((value) => finiteNumber(value, operator)));
  if (operator === 'clamp')
    return Math.min(finiteNumber(values[2], operator), Math.max(finiteNumber(values[1], operator), finiteNumber(values[0], operator)));
  throw new Error(`Unsupported expression operator: ${operator}`);
}

export function evaluateExpressionValue(value: unknown, context: ExpressionContext = {}): ExpressionResult {
  return isExpression(value) ? evaluateExpression(value, context) : value;
}
