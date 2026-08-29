// Historical behavioral oracle only. Production BinScript is compiled by libbinblock.
import type { ImageNode, JsonValue, RecipeDocument, RenderStage } from './recipe-schema.js';

type TokenKind = 'identifier' | 'number' | 'color' | 'string' | 'symbol' | 'eof';

interface Token {
  kind: TokenKind;
  value: string;
  from: number;
  to: number;
  unit?: '%' | 'deg';
}

interface LiteralExpression {
  kind: 'literal';
  value: string | number;
  token: Token;
  from: number;
  to: number;
}

interface IdentifierExpression {
  kind: 'identifier';
  name: string;
  from: number;
  to: number;
}

interface CallExpression {
  kind: 'call';
  name: string;
  args: Expression[];
  named: { name: string; value: Expression }[];
  stops: { color: Expression; offset?: LiteralExpression }[];
  radialShape?: 'circle' | 'ellipse';
  from: number;
  to: number;
}

interface MemberExpression {
  kind: 'member';
  object: Expression;
  name: string;
  args: Expression[];
  from: number;
  to: number;
}

interface ArrayExpression {
  kind: 'array';
  items: Expression[];
  from: number;
  to: number;
}

type Expression = LiteralExpression | IdentifierExpression | CallExpression | MemberExpression | ArrayExpression;

interface ImportStatement {
  kind: 'import';
  preamble: string;
  from: number;
  to: number;
}

interface BindingStatement {
  kind: 'binding';
  name: string;
  expression: Expression;
  from: number;
  to: number;
}

interface ExpressionStatement {
  kind: 'expression';
  expression: Expression;
}

type Statement = ImportStatement | BindingStatement | ExpressionStatement;

export type BinScriptProjectionTarget =
  | { kind: 'color'; from: number; to: number; value: string }
  | {
      kind: 'number';
      from: number;
      to: number;
      value: number;
      property: string;
      min: number;
      max: number;
      step: number;
      suffix: string;
    }
  | { kind: 'stage'; from: number; to: number; at: number; stageRows: string[][] }
  | { kind: 'comparison'; from: number; to: number; at: number; stageRows: string[][]; fixtureSet: string };

export class BinScriptError extends Error {
  constructor(
    message: string,
    readonly from: number,
    readonly to: number,
  ) {
    super(message);
    this.name = 'BinScriptError';
  }
}

function tokenize(source: string): Token[] {
  const tokens: Token[] = [];
  let index = 0;
  while (index < source.length) {
    const character = source[index];
    if (/\s/.test(character)) {
      index += 1;
      continue;
    }
    if (source.startsWith('//', index)) {
      const lineEnd = source.indexOf('\n', index + 2);
      index = lineEnd === -1 ? source.length : lineEnd + 1;
      continue;
    }
    if (source.startsWith(':=', index)) {
      tokens.push({ kind: 'symbol', value: ':=', from: index, to: index + 2 });
      index += 2;
      continue;
    }
    if ('[](),.:;'.includes(character)) {
      tokens.push({ kind: 'symbol', value: character, from: index, to: index + 1 });
      index += 1;
      continue;
    }
    if (character === '#') {
      const match = /^#[\da-fA-F]{6}(?:[\da-fA-F]{2})?/.exec(source.slice(index));
      if (!match) throw new BinScriptError('Expected a color in #rrggbb or #rrggbbaa form.', index, index + 1);
      tokens.push({ kind: 'color', value: match[0], from: index, to: index + match[0].length });
      index += match[0].length;
      continue;
    }
    if (character === '"' || character === "'") {
      const quote = character;
      const from = index;
      index += 1;
      let value = '';
      while (index < source.length && source[index] !== quote) {
        if (source[index] === '\\') {
          index += 1;
          if (index >= source.length) break;
          const escaped = source[index];
          value += escaped === 'n' ? '\n' : escaped === 't' ? '\t' : escaped;
        } else {
          value += source[index];
        }
        index += 1;
      }
      if (source[index] !== quote) throw new BinScriptError('Unterminated string.', from, source.length);
      index += 1;
      tokens.push({ kind: 'string', value, from, to: index });
      continue;
    }
    const numberMatch = /^-?(?:\d+(?:\.\d*)?|\.\d+)(%|deg)?/.exec(source.slice(index));
    if (numberMatch) {
      const unit = numberMatch[1] as '%' | 'deg' | undefined;
      const raw = unit ? numberMatch[0].slice(0, -unit.length) : numberMatch[0];
      tokens.push({ kind: 'number', value: raw, unit, from: index, to: index + numberMatch[0].length });
      index += numberMatch[0].length;
      continue;
    }
    const identifierMatch = /^[A-Za-z_][\w-]*/.exec(source.slice(index));
    if (identifierMatch) {
      tokens.push({ kind: 'identifier', value: identifierMatch[0], from: index, to: index + identifierMatch[0].length });
      index += identifierMatch[0].length;
      continue;
    }
    throw new BinScriptError(`Unexpected character ${JSON.stringify(character)}.`, index, index + 1);
  }
  tokens.push({ kind: 'eof', value: '', from: source.length, to: source.length });
  return tokens;
}

class Parser {
  private index = 0;

  constructor(private readonly tokens: Token[]) {}

  parseProgram(): Statement[] {
    const statements: Statement[] = [];
    while (this.peek().kind !== 'eof') {
      if (this.matches('symbol', ';')) continue;
      if (this.peek().kind === 'identifier' && this.peek().value === 'import') {
        const importToken = this.take();
        const preamble = this.take();
        if (preamble.kind !== 'string') this.fail('Expected an import path string.', preamble);
        statements.push({ kind: 'import', preamble: preamble.value, from: importToken.from, to: preamble.to });
        continue;
      }
      if (this.peek().kind === 'identifier' && this.peek(1).value === ':=') {
        const name = this.take();
        this.take();
        const expression = this.parseExpression();
        statements.push({
          kind: 'binding',
          name: name.value,
          expression,
          from: name.from,
          to: expression.to,
        });
        continue;
      }
      statements.push({ kind: 'expression', expression: this.parseExpression() });
    }
    return statements;
  }

  private parseExpression(): Expression {
    let expression = this.parsePrimary();
    while (this.matches('symbol', '.')) {
      const member = this.expect('identifier', undefined, 'Expected a method name after the dot.');
      this.expect('symbol', '(', `Expected ( after ${member.value}.`);
      const args = this.parseArguments();
      const close = this.expect('symbol', ')', `Expected ) after ${member.value} arguments.`);
      expression = { kind: 'member', object: expression, name: member.value, args, from: expression.from, to: close.to };
    }
    return expression;
  }

  private parsePrimary(): Expression {
    const token = this.take();
    if (token.kind === 'symbol' && token.value === '[') {
      const items: Expression[] = [];
      while (this.peek().value !== ']') {
        if (this.peek().kind === 'eof') this.fail('Expected ] after array items.', this.peek());
        items.push(this.parseExpression());
        if (!this.matches('symbol', ',')) break;
      }
      const close = this.expect('symbol', ']', 'Expected ] after array items.');
      return { kind: 'array', items, from: token.from, to: close.to };
    }
    if (token.kind === 'number') return { kind: 'literal', value: Number(token.value), token, from: token.from, to: token.to };
    if (token.kind === 'color' || token.kind === 'string') {
      return { kind: 'literal', value: token.value, token, from: token.from, to: token.to };
    }
    if (token.kind !== 'identifier') this.fail('Expected an expression.', token);
    if (!this.matches('symbol', '(')) return { kind: 'identifier', name: token.value, from: token.from, to: token.to };
    if (['linear-gradient', 'lin-grad', 'lg', 'radial-gradient', 'rad-grad', 'rg'].includes(token.value)) return this.parseGradient(token);
    const named: { name: string; value: Expression }[] = [];
    const args: Expression[] = [];
    while (this.peek().value !== ')') {
      if (this.peek().kind === 'eof') this.fail(`Expected ) after ${token.value} arguments.`, this.peek());
      if (this.peek().kind === 'identifier' && this.peek(1).value === ':') {
        const name = this.take().value;
        this.take();
        named.push({ name, value: this.parseExpression() });
      } else {
        args.push(this.parseExpression());
      }
      if (!this.matches('symbol', ',')) break;
    }
    const close = this.expect('symbol', ')', `Expected ) after ${token.value} arguments.`);
    return { kind: 'call', name: token.value, args, named, stops: [], from: token.from, to: close.to };
  }

  private parseGradient(name: Token): CallExpression {
    const args: Expression[] = [];
    const named: { name: string; value: Expression }[] = [];
    let radialShape: 'circle' | 'ellipse' | undefined;
    if (['linear-gradient', 'lin-grad', 'lg'].includes(name.value)) {
      args.push(this.parseExpression());
      this.expect('symbol', ',', 'Expected a comma after the gradient angle.');
    } else if (this.peek().kind === 'identifier' && (this.peek().value === 'circle' || this.peek().value === 'ellipse')) {
      radialShape = this.take().value as 'circle' | 'ellipse';
      this.expect('symbol', ',', 'Expected a comma after the radial shape.');
    }
    const stops: { color: Expression; offset?: LiteralExpression }[] = [];
    while (this.peek().value !== ')') {
      if (this.peek().kind === 'identifier' && this.peek(1).value === ':') {
        const option = this.take();
        this.take();
        named.push({ name: option.value, value: this.parseExpression() });
      } else {
        const color = this.parseExpression();
        let offset: LiteralExpression | undefined;
        if (this.peek().kind === 'number') {
          const token = this.take();
          offset = { kind: 'literal', value: Number(token.value), token, from: token.from, to: token.to };
        }
        stops.push({ color, offset });
      }
      if (!this.matches('symbol', ',')) break;
    }
    const close = this.expect('symbol', ')', `Expected ) after ${name.value} stops.`);
    return { kind: 'call', name: name.value, args, named, stops, radialShape, from: name.from, to: close.to };
  }

  private parseArguments(): Expression[] {
    const args: Expression[] = [];
    while (this.peek().value !== ')') {
      if (this.peek().kind === 'eof') this.fail('Expected a closing parenthesis.', this.peek());
      args.push(this.parseExpression());
      if (!this.matches('symbol', ',')) break;
    }
    return args;
  }

  private peek(offset = 0): Token {
    return this.tokens[Math.min(this.index + offset, this.tokens.length - 1)];
  }

  private take(): Token {
    return this.tokens[this.index++];
  }

  private matches(kind: TokenKind, value?: string): boolean {
    const token = this.peek();
    if (token.kind !== kind || (value !== undefined && token.value !== value)) return false;
    this.index += 1;
    return true;
  }

  private expect(kind: TokenKind, value: string | undefined, message: string): Token {
    const token = this.take();
    if (token.kind !== kind || (value !== undefined && token.value !== value)) this.fail(message, token);
    return token;
  }

  private fail(message: string, token: Token): never {
    throw new BinScriptError(message, token.from, Math.max(token.to, token.from + 1));
  }
}

interface PalettePlan {
  kind: 'palette';
  entries: { id: string; value: string }[];
}

interface ImagesPlan {
  kind: 'images';
  images: { node: ImageNode; sizing?: 'radial-circle' | 'radial-ellipse' }[];
}

interface PaletteFillPlan {
  kind: 'palette-fill';
  palette: PalettePlan;
  width: number;
  height: number;
}

interface MaskPlan {
  kind: 'mask';
  source: StageReference;
  mask: StageReference;
  mode: 'multiply' | 'replace';
}

interface TransformPlan {
  kind: 'transform';
  source: StageReference;
  operation: 'resize' | 'rotate' | 'opacity' | 'tint' | 'rgb' | 'invert-alpha' | 'crop' | 'canvas';
  values: (number | string)[];
}

interface CompositePlan {
  kind: 'composite';
  destination: StageReference;
  source: StageReference;
  offsetX: number;
  offsetY: number;
  opacity: number;
}

interface StageReference {
  kind: 'stage';
  stageId: string;
  cardinality: 'one' | 'many';
}

interface FunctionReference {
  kind: 'function';
  name: string;
}

interface ArrayValue {
  kind: 'array';
  items: RuntimeValue[];
}

interface FixtureReference {
  kind: 'fixtures';
  fixtureSet: string;
}

interface ComparisonValue {
  kind: 'comparison';
  generated: RuntimeValue;
  fixtures: FixtureReference;
}

type RuntimeValue =
  | PalettePlan
  | ImagesPlan
  | PaletteFillPlan
  | MaskPlan
  | TransformPlan
  | CompositePlan
  | StageReference
  | FunctionReference
  | ArrayValue
  | FixtureReference
  | ComparisonValue
  | string
  | number;

const NAMED_COLORS: Readonly<Record<string, string>> = {
  black: '#000000',
  transparent: '#00000000',
  'transparent-black': '#00000000',
  'transparent-white': '#ffffff00',
  white: '#ffffff',
};

function identifier(name: string): string {
  const normalized = name
    .replace(/([a-z\d])([A-Z])/g, '$1-$2')
    .replace(/_/g, '-')
    .toLowerCase();
  if (!/^[a-z][a-z0-9-]*$/.test(normalized)) throw new Error(`Invalid binding name: ${name}`);
  return normalized;
}

function literalColor(expression: Expression): string {
  if (expression.kind === 'literal' && typeof expression.value === 'string' && /^#[\da-f]{6}(?:[\da-f]{2})?$/i.test(expression.value)) {
    return expression.value;
  }
  if (expression.kind === 'identifier' && NAMED_COLORS[expression.name]) return NAMED_COLORS[expression.name];
  throw new BinScriptError('Expected a hex color or the name black, white, or transparent.', expression.from, expression.to);
}

function gradientStops(expression: CallExpression, resolveColor: (expression: Expression) => string): { offset: number; color: string }[] {
  if (expression.stops.length < 2) throw new BinScriptError('Gradients need at least two color stops.', expression.from, expression.to);
  const offsets = expression.stops.map((stop) => {
    if (!stop.offset) return undefined;
    if (stop.offset.token.unit !== '%') {
      throw new BinScriptError('Gradient stop positions must use percentages.', stop.offset.token.from, stop.offset.token.to);
    }
    const offset = Number(stop.offset.value) / 100;
    if (offset < 0 || offset > 1) {
      throw new BinScriptError('Gradient stop positions must be between 0% and 100%.', stop.offset.token.from, stop.offset.token.to);
    }
    return offset;
  });
  offsets[0] ??= 0;
  offsets[offsets.length - 1] ??= 1;
  let previous = offsets[0]!;
  for (let index = 1; index < offsets.length; index += 1) {
    const offset = offsets[index];
    if (offset === undefined) continue;
    const clamped = Math.max(previous, offset);
    offsets[index] = clamped;
    previous = clamped;
  }
  for (let index = 1; index < offsets.length - 1; index += 1) {
    if (offsets[index] !== undefined) continue;
    const start = index - 1;
    let end = index + 1;
    while (offsets[end] === undefined) end += 1;
    const span = end - start;
    for (let fill = 1; fill < span; fill += 1) {
      offsets[start + fill] = offsets[start]! + ((offsets[end]! - offsets[start]!) * fill) / span;
    }
    index = end - 1;
  }
  return expression.stops.map((stop, index) => ({ offset: offsets[index]!, color: resolveColor(stop.color) }));
}

function resizeImages(plan: ImagesPlan, width: number, height: number): ImagesPlan {
  return {
    kind: 'images',
    images: plan.images.map((image) => {
      if (image.node.op === 'gradient' && image.node.shape === 'ellipse' && image.sizing) {
        const radius = Math.hypot(width / 2, height / 2);
        return {
          ...image,
          node: {
            ...image.node,
            width,
            height,
            radiusX: image.sizing === 'radial-circle' ? radius : (width / 2) * Math.SQRT2,
            radiusY: image.sizing === 'radial-circle' ? radius : (height / 2) * Math.SQRT2,
          },
        };
      }
      if (image.node.op === 'fill' || image.node.op === 'gradient') {
        return { ...image, node: { ...image.node, width, height } };
      }
      if (image.node.op === 'alpha-field') return { ...image, node: { ...image.node, width, height } };
      return { node: { op: 'resize', source: image.node, width, height, filter: 'lanczos3' } };
    }),
  };
}

export interface CompiledBinScript {
  document: RecipeDocument;
  projections: BinScriptProjectionTarget[];
}

export function compileBinScript(source: string): CompiledBinScript {
  const statements = new Parser(tokenize(source)).parseProgram();
  const environment = new Map<string, RuntimeValue>();
  const stages: RenderStage[] = [];
  const projections: BinScriptProjectionTarget[] = [];
  const outputs: string[] = [];
  const assets: NonNullable<RecipeDocument['assets']> = {};
  let lastStage: string | undefined;

  const resolveColor = (expression: Expression): string => {
    if (expression.kind === 'identifier') {
      const value = environment.get(expression.name);
      if (typeof value === 'string' && /^#[\da-f]{6}(?:[\da-f]{2})?$/i.test(value)) return value;
    }
    return literalColor(expression);
  };

  const mapStages = (value: RuntimeValue, expression: Expression, mapper: (stage: StageReference) => RuntimeValue): RuntimeValue => {
    if (typeof value !== 'object') {
      throw new BinScriptError('This operation expects a bound image set.', expression.from, expression.to);
    }
    if (value.kind === 'stage') return mapper(value);
    if (value.kind === 'array') return { kind: 'array', items: value.items.map((item) => mapStages(item, expression, mapper)) };
    throw new BinScriptError('This operation expects a bound image set.', expression.from, expression.to);
  };

  const combineStages = (
    left: RuntimeValue,
    right: RuntimeValue,
    expression: Expression,
    combine: (left: StageReference, right: StageReference) => RuntimeValue,
  ): RuntimeValue => {
    if (typeof left === 'object' && left.kind === 'array') {
      return { kind: 'array', items: left.items.map((item) => combineStages(item, right, expression, combine)) };
    }
    if (typeof right === 'object' && right.kind === 'array') {
      return { kind: 'array', items: right.items.map((item) => combineStages(left, item, expression, combine)) };
    }
    if (typeof left === 'object' && left.kind === 'stage' && typeof right === 'object' && right.kind === 'stage') {
      return combine(left, right);
    }
    throw new BinScriptError('This operation expects bound image sets.', expression.from, expression.to);
  };

  const projectColors = (expression: Expression): void => {
    if (
      expression.kind === 'literal' &&
      expression.token.kind === 'color' &&
      /^#[\da-f]{6}(?:[\da-f]{2})?$/i.test(String(expression.value))
    ) {
      projections.push({ kind: 'color', from: expression.token.from, to: expression.token.to, value: String(expression.value) });
    }
    if (expression.kind === 'identifier' && NAMED_COLORS[expression.name] && !environment.has(expression.name)) {
      projections.push({ kind: 'color', from: expression.from, to: expression.to, value: NAMED_COLORS[expression.name] });
    }
    if (expression.kind === 'call') {
      for (const argument of expression.args) projectColors(argument);
      for (const argument of expression.named) projectColors(argument.value);
      for (const stop of expression.stops) projectColors(stop.color);
    }
    if (expression.kind === 'member') {
      projectColors(expression.object);
      for (const argument of expression.args) projectColors(argument);
    }
    if (expression.kind === 'array') {
      for (const item of expression.items) projectColors(item);
    }
  };

  const evaluate = (expression: Expression): RuntimeValue => {
    if (expression.kind === 'literal') return expression.value;
    if (expression.kind === 'array') return { kind: 'array', items: expression.items.map(evaluate) };
    if (expression.kind === 'identifier') {
      const value = environment.get(expression.name);
      if (value !== undefined) return value;
      if (expression.name === 'fill') return { kind: 'function', name: 'fill' };
      if (NAMED_COLORS[expression.name]) return NAMED_COLORS[expression.name];
      if (expression.name === 'multiply' || expression.name === 'replace') return expression.name;
      throw new BinScriptError(`Unknown name: ${expression.name}`, expression.from, expression.to);
    }
    if (expression.kind === 'call') {
      if (expression.name === 'collect') {
        if (expression.named.length > 0 || expression.args.length !== 1) {
          throw new BinScriptError('collect() expects one array.', expression.from, expression.to);
        }
        const value = evaluate(expression.args[0]);
        if (typeof value !== 'object' || value.kind !== 'array') {
          throw new BinScriptError('collect() expects one array.', expression.from, expression.to);
        }
        return value;
      }
      if (expression.name === 'fixtures') {
        if (expression.named.length > 0 || expression.args.length !== 1) {
          throw new BinScriptError('fixtures() expects one fixture-set name.', expression.from, expression.to);
        }
        const fixtureSet = evaluate(expression.args[0]);
        if (fixtureSet !== 'gradient-masks' && fixtureSet !== 'gradient-variants-blue-hi') {
          throw new BinScriptError(
            'Unknown fixture set. Available: gradient-masks, gradient-variants-blue-hi.',
            expression.from,
            expression.to,
          );
        }
        return { kind: 'fixtures', fixtureSet };
      }
      if (expression.name === 'compare') {
        if (expression.named.length > 0 || expression.args.length !== 2) {
          throw new BinScriptError('compare() expects a generated collection and fixtures.', expression.from, expression.to);
        }
        const generated = evaluate(expression.args[0]);
        const fixtures = evaluate(expression.args[1]);
        if (typeof fixtures !== 'object' || fixtures.kind !== 'fixtures') {
          throw new BinScriptError('compare() second argument must be fixtures().', expression.from, expression.to);
        }
        if (!displayedStages(generated)) {
          throw new BinScriptError('compare() first argument must be bound image sets.', expression.from, expression.to);
        }
        return { kind: 'comparison', generated, fixtures };
      }
      if (expression.name === 'union') {
        if (expression.named.length > 0 || expression.args.length === 0) {
          throw new BinScriptError('union() expects one or more collections.', expression.from, expression.to);
        }
        const items: RuntimeValue[] = [];
        for (const argument of expression.args) {
          const value = evaluate(argument);
          if (typeof value === 'object' && value.kind === 'array') items.push(...value.items);
          else items.push(value);
        }
        return { kind: 'array', items };
      }
      if (expression.name === 'product') {
        if (expression.named.length > 0 || expression.args.length !== 2) {
          throw new BinScriptError('product() expects two collections.', expression.from, expression.to);
        }
        const left = evaluate(expression.args[0]);
        const right = evaluate(expression.args[1]);
        if (typeof left !== 'object' || left.kind !== 'array' || typeof right !== 'object' || right.kind !== 'array') {
          throw new BinScriptError('product() expects two collections.', expression.from, expression.to);
        }
        return {
          kind: 'array',
          items: left.items.map((leftItem) => ({
            kind: 'array',
            items: right.items.map((rightItem) => ({ kind: 'array', items: [leftItem, rightItem] })),
          })),
        };
      }
      if (expression.name === 'palette') {
        if (expression.named.length === 0 || expression.args.length > 0) {
          throw new BinScriptError('palette() only accepts named colors such as coral: #ff6030.', expression.from, expression.to);
        }
        return {
          kind: 'palette',
          entries: expression.named.map(({ name, value }) => ({ id: identifier(name), value: resolveColor(value) })),
        };
      }
      if (expression.name === 'fill') {
        if (expression.args.length !== 1 || expression.named.length > 0) {
          throw new BinScriptError('fill() expects one color.', expression.from, expression.to);
        }
        return { kind: 'images', images: [{ node: { op: 'fill', width: 64, height: 64, color: resolveColor(expression.args[0]) } }] };
      }
      if (expression.name === 'square-gradient' || expression.name === 'border-gradient') {
        if (expression.args.length > 0)
          throw new BinScriptError(`${expression.name}() uses named arguments.`, expression.from, expression.to);
        const named = new Map(expression.named.map(({ name, value }) => [name, value]));
        const numberOption = (name: string, fallback: number): number => {
          const argument = named.get(name);
          if (!argument) return fallback;
          const value = evaluate(argument);
          if (typeof value !== 'number')
            throw new BinScriptError(`${expression.name}() ${name} must be a number.`, argument.from, argument.to);
          return value;
        };
        const textOption = (name: string, fallback: string): string => {
          const argument = named.get(name);
          if (!argument) return fallback;
          const value = evaluate(argument);
          if (typeof value !== 'string')
            throw new BinScriptError(`${expression.name}() ${name} must be a string.`, argument.from, argument.to);
          return value;
        };
        const center = named.get('center');
        const centerValue = center ? evaluate(center) : undefined;
        const centerItems = centerValue && typeof centerValue === 'object' && centerValue.kind === 'array' ? centerValue.items : [32, 32];
        if (centerItems.length !== 2 || centerItems.some((value) => typeof value !== 'number')) {
          throw new BinScriptError(
            `${expression.name}() center must contain two numbers.`,
            center?.from ?? expression.from,
            center?.to ?? expression.to,
          );
        }
        const levelsExpression = named.get('levels');
        const levelsValue = levelsExpression ? evaluate(levelsExpression) : undefined;
        const levels =
          levelsValue && typeof levelsValue === 'object' && levelsValue.kind === 'array'
            ? levelsValue.items.map((value) => {
                if (typeof value !== 'number')
                  throw new BinScriptError('border-gradient() levels must be numbers.', levelsExpression!.from, levelsExpression!.to);
                return value;
              })
            : undefined;
        const colorExpression = named.get('color');
        return {
          kind: 'images',
          images: [
            {
              node: {
                op: 'alpha-field',
                width: 64,
                height: 64,
                metric: expression.name === 'square-gradient' ? 'chebyshev' : 'border',
                centerX: centerItems[0] as number,
                centerY: centerItems[1] as number,
                radius: numberOption('radius', 32),
                direction: textOption('direction', expression.name === 'square-gradient' ? 'in' : 'out') as 'in' | 'out',
                easing: textOption('easing', 'linear') as 'linear' | 'smoothstep' | 'legacy',
                color: colorExpression ? resolveColor(colorExpression) : '#000000',
                ...(levels ? { levels } : {}),
              },
            },
          ],
        };
      }
      if (expression.name === 'linear-gradient' || expression.name === 'lin-grad' || expression.name === 'lg') {
        if (expression.args.length !== 1) {
          throw new BinScriptError('linear-gradient() expects an angle followed by color stops.', expression.from, expression.to);
        }
        const named = new Map(expression.named.map(({ name, value }) => [name, value]));
        const easingValue = named.get('easing') ? evaluate(named.get('easing')!) : 'linear';
        const extentValue = named.get('extent') ? evaluate(named.get('extent')!) : undefined;
        if (!['linear', 'smoothstep', 'legacy'].includes(String(easingValue))) {
          throw new BinScriptError('linear-gradient() easing must be linear, smoothstep, or legacy.', expression.from, expression.to);
        }
        if (extentValue !== undefined && (typeof extentValue !== 'number' || extentValue <= 0)) {
          throw new BinScriptError('linear-gradient() extent must be positive.', expression.from, expression.to);
        }
        const angleExpression = expression.args[0];
        const angleValue = evaluate(angleExpression);
        if (typeof angleValue !== 'number')
          throw new BinScriptError('The gradient angle must be a number.', angleExpression.from, angleExpression.to);
        const angle = angleValue;
        if (angleExpression.kind === 'literal' && angleExpression.token.unit !== 'deg') {
          throw new BinScriptError('Linear gradient angles must use deg.', angleExpression.from, angleExpression.to);
        }
        if (angleExpression.kind === 'literal') {
          projections.push({
            kind: 'number',
            from: angleExpression.token.from,
            to: angleExpression.token.to,
            value: angle,
            property: 'angle',
            min: -360,
            max: 360,
            step: 1,
            suffix: 'deg',
          });
        }
        return {
          kind: 'images',
          images: [
            {
              node: {
                op: 'gradient',
                shape: 'linear',
                width: 64,
                height: 64,
                angle,
                ...(typeof extentValue === 'number' ? { extent: extentValue } : {}),
                easing: easingValue as 'linear' | 'smoothstep' | 'legacy',
                stops: gradientStops(expression, resolveColor),
              },
            },
          ],
        };
      }
      if (expression.name === 'radial-gradient' || expression.name === 'rad-grad' || expression.name === 'rg') {
        if (expression.args.length > 0) {
          throw new BinScriptError('radial-gradient() expects an optional shape followed by color stops.', expression.from, expression.to);
        }
        const named = new Map(expression.named.map(({ name, value }) => [name, value]));
        const centerExpression = named.get('center');
        const centerValue = centerExpression ? evaluate(centerExpression) : undefined;
        const center = centerValue && typeof centerValue === 'object' && centerValue.kind === 'array' ? centerValue.items : undefined;
        if (center && (center.length !== 2 || center.some((value) => typeof value !== 'number'))) {
          throw new BinScriptError('radial-gradient() center must contain two numbers.', centerExpression!.from, centerExpression!.to);
        }
        const radiusValue = named.get('radius') ? evaluate(named.get('radius')!) : undefined;
        const easingValue = named.get('easing') ? evaluate(named.get('easing')!) : 'linear';
        const roundingValue = named.get('rounding') ? evaluate(named.get('rounding')!) : 'nearest';
        if (radiusValue !== undefined && (typeof radiusValue !== 'number' || radiusValue <= 0)) {
          throw new BinScriptError('radial-gradient() radius must be positive.', expression.from, expression.to);
        }
        if (!['linear', 'smoothstep', 'legacy'].includes(String(easingValue)) || !['nearest', 'legacy'].includes(String(roundingValue))) {
          throw new BinScriptError('radial-gradient() has an invalid easing or rounding option.', expression.from, expression.to);
        }
        const ellipseRadius = 32 * Math.SQRT2;
        const radius =
          typeof radiusValue === 'number' ? radiusValue : expression.radialShape === 'circle' ? Math.hypot(32, 32) : ellipseRadius;
        return {
          kind: 'images',
          images: [
            {
              node: {
                op: 'gradient',
                shape: 'ellipse',
                width: 64,
                height: 64,
                ...(center ? { centerX: center[0] as number, centerY: center[1] as number } : {}),
                radiusX: radius,
                radiusY: radius,
                rotation: 0,
                easing: easingValue as 'linear' | 'smoothstep' | 'legacy',
                legacyRadialRounding: roundingValue === 'legacy',
                stops: gradientStops(expression, resolveColor),
              },
              ...(radiusValue === undefined && !center
                ? { sizing: expression.radialShape === 'circle' ? 'radial-circle' : 'radial-ellipse' }
                : {}),
            },
          ],
        };
      }
      throw new BinScriptError(`Unknown function: ${expression.name}`, expression.from, expression.to);
    }

    const object = evaluate(expression.object);
    if (typeof object !== 'object') {
      throw new BinScriptError(`${expression.name}() cannot be applied to this value.`, expression.from, expression.to);
    }
    if (expression.name === 'select') {
      if (object.kind !== 'array' || expression.args.length !== 1) {
        throw new BinScriptError('select() expects a collection and one index.', expression.from, expression.to);
      }
      const index = evaluate(expression.args[0]);
      if (typeof index !== 'number' || !Number.isInteger(index)) {
        throw new BinScriptError('select() expects an integer index.', expression.from, expression.to);
      }
      const selected = object.items.at(index);
      if (selected === undefined) throw new BinScriptError('select() index is out of range.', expression.from, expression.to);
      return selected;
    }
    if (expression.name === 'slice') {
      if (object.kind !== 'array' || expression.args.length < 1 || expression.args.length > 2) {
        throw new BinScriptError('slice() expects a collection, start, and optional end.', expression.from, expression.to);
      }
      const start = evaluate(expression.args[0]);
      const end = expression.args[1] ? evaluate(expression.args[1]) : undefined;
      if (
        typeof start !== 'number' ||
        !Number.isInteger(start) ||
        (end !== undefined && (typeof end !== 'number' || !Number.isInteger(end)))
      ) {
        throw new BinScriptError('slice() bounds must be integers.', expression.from, expression.to);
      }
      return { kind: 'array', items: object.items.slice(start, end) };
    }
    if (expression.name === 'map') {
      if (expression.args.length !== 1) throw new BinScriptError('map() expects one function.', expression.from, expression.to);
      const mapper = expression.args[0] ? evaluate(expression.args[0]) : undefined;
      if (object.kind !== 'palette' || !mapper || typeof mapper !== 'object' || mapper.kind !== 'function' || mapper.name !== 'fill') {
        throw new BinScriptError('map(fill) currently expects a palette.', expression.from, expression.to);
      }
      return { kind: 'palette-fill', palette: object, width: 64, height: 64 };
    }
    if (expression.name === 'size') {
      if (expression.args.length < 1 || expression.args.length > 2) {
        throw new BinScriptError('size() expects width and optional height.', expression.from, expression.to);
      }
      const widthExpression = expression.args[0];
      const heightExpression = expression.args[1] ?? widthExpression;
      const widthValue = evaluate(widthExpression);
      const heightValue = evaluate(heightExpression);
      if (typeof widthValue !== 'number' || typeof heightValue !== 'number') {
        throw new BinScriptError('size() dimensions must resolve to numbers.', expression.from, expression.to);
      }
      const width = widthValue;
      const height = heightValue;
      if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) {
        throw new BinScriptError('size() dimensions must be positive integers.', expression.from, expression.to);
      }
      if (widthExpression.kind === 'literal') {
        projections.push({
          kind: 'number',
          from: widthExpression.from,
          to: widthExpression.to,
          value: width,
          property: expression.args.length === 1 ? 'size' : 'width',
          min: 1,
          max: 256,
          step: 1,
          suffix: '',
        });
      }
      if (expression.args.length === 2 && heightExpression.kind === 'literal') {
        projections.push({
          kind: 'number',
          from: heightExpression.from,
          to: heightExpression.to,
          value: height,
          property: 'height',
          min: 1,
          max: 256,
          step: 1,
          suffix: '',
        });
      }
      if (object.kind === 'images') return resizeImages(object, width, height);
      if (object.kind === 'palette-fill') return { ...object, width, height };
      if (object.kind === 'stage' || object.kind === 'array') {
        return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'resize', values: [width, height] }));
      }
      throw new BinScriptError('size() must be applied before an image set is rendered.', expression.from, expression.to);
    }
    if (expression.name === 'resize') {
      if (expression.args.length < 1 || expression.args.length > 2) {
        throw new BinScriptError('resize() expects width and optional height.', expression.from, expression.to);
      }
      const width = evaluate(expression.args[0]);
      const height = evaluate(expression.args[1] ?? expression.args[0]);
      if (
        typeof width !== 'number' ||
        typeof height !== 'number' ||
        !Number.isInteger(width) ||
        !Number.isInteger(height) ||
        width < 1 ||
        height < 1
      ) {
        throw new BinScriptError('resize() dimensions must be positive integers.', expression.from, expression.to);
      }
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'resize', values: [width, height] }));
    }
    if (expression.name === 'rotate') {
      if (expression.args.length !== 1) throw new BinScriptError('rotate() expects quarter turns.', expression.from, expression.to);
      const turns = evaluate(expression.args[0]);
      if (typeof turns !== 'number' || !Number.isInteger(turns)) {
        throw new BinScriptError('rotate() expects an integer number of quarter turns.', expression.from, expression.to);
      }
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'rotate', values: [turns] }));
    }
    if (expression.name === 'opacity') {
      if (expression.args.length !== 1) throw new BinScriptError('opacity() expects one number.', expression.from, expression.to);
      const opacity = evaluate(expression.args[0]);
      if (typeof opacity !== 'number' || opacity < 0 || opacity > 1) {
        throw new BinScriptError('opacity() must be between 0 and 1.', expression.from, expression.to);
      }
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'opacity', values: [opacity] }));
    }
    if (expression.name === 'tint') {
      if (expression.args.length !== 1) throw new BinScriptError('tint() expects one color.', expression.from, expression.to);
      const color = resolveColor(expression.args[0]);
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'tint', values: [color] }));
    }
    if (expression.name === 'rgb') {
      if (expression.args.length !== 1) throw new BinScriptError('rgb() expects one color.', expression.from, expression.to);
      const color = resolveColor(expression.args[0]);
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'rgb', values: [color] }));
    }
    if (expression.name === 'invert-alpha') {
      if (expression.args.length !== 0)
        throw new BinScriptError('invert-alpha() does not accept arguments.', expression.from, expression.to);
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'invert-alpha', values: [] }));
    }
    if (expression.name === 'crop') {
      if (expression.args.length !== 4) throw new BinScriptError('crop() expects x, y, width, and height.', expression.from, expression.to);
      const values = expression.args.map(evaluate);
      if (values.some((value) => typeof value !== 'number' || !Number.isInteger(value))) {
        throw new BinScriptError('crop() expects integer arguments.', expression.from, expression.to);
      }
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'crop', values: values as number[] }));
    }
    if (expression.name === 'canvas') {
      if (expression.args.length < 1 || expression.args.length > 4) {
        throw new BinScriptError('canvas() expects width, optional height, x, and y.', expression.from, expression.to);
      }
      const width = evaluate(expression.args[0]);
      const height = expression.args[1] ? evaluate(expression.args[1]) : width;
      const x = expression.args[2] ? evaluate(expression.args[2]) : 0;
      const y = expression.args[3] ? evaluate(expression.args[3]) : 0;
      if (
        typeof width !== 'number' ||
        typeof height !== 'number' ||
        typeof x !== 'number' ||
        typeof y !== 'number' ||
        ![width, height, x, y].every(Number.isInteger) ||
        width < 1 ||
        height < 1
      ) {
        throw new BinScriptError('canvas() expects integer dimensions and offsets.', expression.from, expression.to);
      }
      return mapStages(object, expression, (source) => ({ kind: 'transform', source, operation: 'canvas', values: [width, height, x, y] }));
    }
    if (expression.name === 'mask') {
      if (expression.args.length < 1 || expression.args.length > 2) {
        throw new BinScriptError('mask() expects an image set and optional mode.', expression.from, expression.to);
      }
      const mask = expression.args[0] ? evaluate(expression.args[0]) : undefined;
      const modeValue = expression.args[1] ? evaluate(expression.args[1]) : 'replace';
      if (modeValue !== 'replace' && modeValue !== 'multiply') {
        throw new BinScriptError('mask() mode must be replace or multiply.', expression.from, expression.to);
      }
      if (!mask) throw new BinScriptError('mask() expects two previously bound image sets.', expression.from, expression.to);
      return combineStages(object, mask, expression, (sourceStage, maskStage) => ({
        kind: 'mask',
        source: sourceStage,
        mask: maskStage,
        mode: modeValue,
      }));
    }
    if (expression.name === 'over') {
      if (expression.args.length < 1 || expression.args.length > 4) {
        throw new BinScriptError('over() expects a source and optional x, y, and opacity.', expression.from, expression.to);
      }
      const source = expression.args[0] ? evaluate(expression.args[0]) : undefined;
      const offsetX = expression.args[1] ? evaluate(expression.args[1]) : 0;
      const offsetY = expression.args[2] ? evaluate(expression.args[2]) : 0;
      const opacity = expression.args[3] ? evaluate(expression.args[3]) : 1;
      if (!source || typeof offsetX !== 'number' || typeof offsetY !== 'number' || typeof opacity !== 'number') {
        throw new BinScriptError('over() offsets and opacity must be numbers.', expression.from, expression.to);
      }
      return combineStages(object, source, expression, (destination, sourceStage) => ({
        kind: 'composite',
        destination,
        source: sourceStage,
        offsetX,
        offsetY,
        opacity,
      }));
    }
    if (expression.name === 'preview') {
      if (expression.args.length !== 0) throw new BinScriptError('preview() does not accept arguments.', expression.from, expression.to);
      if (object.kind !== 'stage' && object.kind !== 'array') {
        throw new BinScriptError('preview() expects a bound image set.', expression.from, expression.to);
      }
      return object;
    }
    throw new BinScriptError(`Unknown method: ${expression.name}`, expression.from, expression.to);
  };

  const displayedStages = (value: RuntimeValue): { stageIds: string[]; rows: string[][] } | undefined => {
    if (typeof value !== 'object') return undefined;
    if (value.kind === 'stage') return { stageIds: [value.stageId], rows: [[value.stageId]] };
    if (value.kind !== 'array') return undefined;
    if (value.items.every((item) => typeof item === 'object' && item.kind === 'stage' && item.cardinality === 'one')) {
      const stageIds = value.items.map((item) => (item as StageReference).stageId);
      return { stageIds, rows: [stageIds] };
    }
    const stageIds: string[] = [];
    const rows: string[][] = [];
    for (const item of value.items) {
      const nested = displayedStages(item);
      if (!nested) return undefined;
      stageIds.push(...nested.stageIds);
      rows.push(...nested.rows);
    }
    return { stageIds, rows };
  };

  const materialize = (value: RuntimeValue, bindingName: string, path: number[], from: number, to: number): RuntimeValue => {
    if (typeof value !== 'object') throw new BinScriptError('Bindings must produce a palette or image set.', from, to);
    if (value.kind === 'stage' || value.kind === 'palette' || value.kind === 'function') return value;
    if (value.kind === 'array') {
      return {
        kind: 'array',
        items: value.items.map((item, index) => materialize(item, bindingName, [...path, index + 1], from, to)),
      };
    }

    const stageId = identifier([bindingName, ...path].join('-'));
    if (stages.some((stage) => stage.id === stageId)) {
      throw new BinScriptError(`Binding names normalize to the same stage: ${stageId}`, from, to);
    }
    let stage: RenderStage;
    let cardinality: StageReference['cardinality'];
    if (value.kind === 'palette-fill') {
      stage = {
        type: 'render',
        id: stageId,
        forEach: { color: { source: 'values', values: value.palette.entries as JsonValue[] } },
        key: ['get', 'id', ['var', 'color']],
        path: ['concat', 'notebook/', stageId, '/', ['get', 'id', ['var', 'color']], '.png'],
        properties: { color: ['get', 'value', ['var', 'color']] },
        image: { op: 'fill', width: value.width, height: value.height, color: ['get', 'value', ['var', 'color']] },
      };
      cardinality = 'many';
    } else if (value.kind === 'images') {
      if (value.images.length !== 1) throw new BinScriptError('This binding must produce one image template.', from, to);
      stage = {
        type: 'render',
        id: stageId,
        forEach: {},
        key: stageId,
        path: `notebook/${stageId}.png`,
        properties: {},
        image: value.images[0].node,
      };
      cardinality = 'one';
    } else if (value.kind === 'mask') {
      stage = {
        type: 'render',
        id: stageId,
        forEach: {
          source: { source: 'stage', stage: value.source.stageId },
          mask: { source: 'stage', stage: value.mask.stageId },
        },
        key: ['concat', ['get', 'key', ['var', 'source']], '-', ['get', 'key', ['var', 'mask']]],
        path: ['concat', 'notebook/', stageId, '/', ['get', 'key', ['var', 'source']], '-', ['get', 'key', ['var', 'mask']], '.png'],
        properties: {},
        image: {
          op: 'apply-mask',
          source: { op: 'input', binding: 'source' },
          mask: { op: 'input', binding: 'mask' },
          mode: value.mode,
        },
      };
      cardinality = value.source.cardinality === 'many' || value.mask.cardinality === 'many' ? 'many' : 'one';
    } else if (value.kind === 'transform') {
      let image: ImageNode;
      if (value.operation === 'resize') {
        image = {
          op: 'resize',
          source: { op: 'input', binding: 'source' },
          width: Number(value.values[0]),
          height: Number(value.values[1]),
          filter: 'lanczos3',
        };
      } else if (value.operation === 'rotate') {
        image = { op: 'rotate', source: { op: 'input', binding: 'source' }, turns: Number(value.values[0]) };
      } else if (value.operation === 'opacity') {
        image = { op: 'opacity', source: { op: 'input', binding: 'source' }, opacity: Number(value.values[0]) };
      } else if (value.operation === 'tint') {
        image = { op: 'tint-chroma', source: { op: 'input', binding: 'source' }, color: String(value.values[0]) };
      } else if (value.operation === 'rgb') {
        image = { op: 'set-visible-rgb', source: { op: 'input', binding: 'source' }, color: String(value.values[0]) };
      } else if (value.operation === 'invert-alpha') {
        image = { op: 'invert-alpha', source: { op: 'input', binding: 'source' } };
      } else if (value.operation === 'crop') {
        image = {
          op: 'crop',
          source: { op: 'input', binding: 'source' },
          x: Number(value.values[0]),
          y: Number(value.values[1]),
          width: Number(value.values[2]),
          height: Number(value.values[3]),
        };
      } else {
        image = {
          op: 'composite',
          destination: { op: 'fill', width: Number(value.values[0]), height: Number(value.values[1]), color: '#00000000' },
          source: { op: 'input', binding: 'source' },
          offsetX: Number(value.values[2]),
          offsetY: Number(value.values[3]),
          opacity: 1,
        };
      }
      stage = {
        type: 'render',
        id: stageId,
        forEach: { source: { source: 'stage', stage: value.source.stageId } },
        key: ['get', 'key', ['var', 'source']],
        path: ['concat', 'notebook/', stageId, '/', ['get', 'key', ['var', 'source']], '.png'],
        properties: {},
        image,
      };
      cardinality = value.source.cardinality;
    } else if (value.kind === 'composite') {
      stage = {
        type: 'render',
        id: stageId,
        forEach: {
          destination: { source: 'stage', stage: value.destination.stageId },
          source: { source: 'stage', stage: value.source.stageId },
        },
        key: ['concat', ['get', 'key', ['var', 'destination']], '-', ['get', 'key', ['var', 'source']]],
        path: ['concat', 'notebook/', stageId, '/', ['get', 'key', ['var', 'destination']], '-', ['get', 'key', ['var', 'source']], '.png'],
        properties: {},
        image: {
          op: 'composite',
          destination: { op: 'input', binding: 'destination' },
          source: { op: 'input', binding: 'source' },
          offsetX: value.offsetX,
          offsetY: value.offsetY,
          opacity: value.opacity,
        },
      };
      cardinality = value.destination.cardinality === 'many' || value.source.cardinality === 'many' ? 'many' : 'one';
    } else {
      throw new BinScriptError('Bindings must produce a palette or image set.', from, to);
    }
    stages.push(stage);
    lastStage = stageId;
    return { kind: 'stage', stageId, cardinality };
  };

  for (const statement of statements) {
    if (statement.kind === 'import') {
      if (statement.preamble !== 'bingen/basic' && statement.preamble !== 'bingen/reference-set') {
        throw new BinScriptError(`Unknown preamble: ${statement.preamble}`, statement.from, statement.to);
      }
      continue;
    }
    projectColors(statement.expression);
    if (statement.kind === 'expression') {
      const value = evaluate(statement.expression);
      if (typeof value === 'object' && value.kind === 'comparison') {
        const display = displayedStages(value.generated);
        if (!display?.stageIds.length) {
          throw new BinScriptError('Comparison has no generated image sets.', statement.expression.from, statement.expression.to);
        }
        projections.push({
          kind: 'comparison',
          from: statement.expression.from,
          to: statement.expression.to,
          at: statement.expression.to,
          stageRows: display.rows,
          fixtureSet: value.fixtures.fixtureSet,
        });
        continue;
      }
      const display = displayedStages(value);
      if (!display?.stageIds.length) {
        throw new BinScriptError(
          'Standalone expressions must resolve to one or more bound image sets.',
          statement.expression.from,
          statement.expression.to,
        );
      }
      outputs.push(...display.stageIds);
      projections.push({
        kind: 'stage',
        from: statement.expression.from,
        to: statement.expression.to,
        at: statement.expression.to,
        stageRows: display.rows,
      });
      continue;
    }
    if (environment.has(statement.name)) throw new BinScriptError(`Duplicate binding: ${statement.name}`, statement.from, statement.to);
    const value = evaluate(statement.expression);
    if (typeof value === 'number') {
      environment.set(statement.name, value);
      if (statement.expression.kind === 'literal') {
        projections.push({
          kind: 'number',
          from: statement.expression.from,
          to: statement.expression.to,
          value,
          property: statement.name,
          min: statement.name === 'size' ? 1 : -256,
          max: 256,
          step: Number.isInteger(value) ? 1 : 0.05,
          suffix: statement.expression.token.unit ?? '',
        });
      }
      continue;
    }
    if (typeof value === 'string' && /^#[\da-f]{6}(?:[\da-f]{2})?$/i.test(value)) {
      environment.set(statement.name, value);
      continue;
    }
    if (typeof value !== 'object') {
      throw new BinScriptError('Bindings must produce a palette or image set.', statement.from, statement.to);
    }
    if (value.kind === 'palette') {
      environment.set(statement.name, value);
      continue;
    }
    if (value.kind === 'comparison' || value.kind === 'fixtures') {
      environment.set(statement.name, value);
      continue;
    }
    environment.set(statement.name, materialize(value, statement.name, [], statement.from, statement.to));
  }

  if (stages.length === 0) throw new BinScriptError('BinScript needs at least one image binding.', 0, Math.min(source.length, 1));
  const selectedOutputs = outputs.length > 0 ? [...new Set(outputs)] : lastStage ? [lastStage] : [];
  return {
    document: {
      format: 'bin-block-recipe/v1',
      profile: 'numeric-srgb/v1',
      metadata: { name: 'BinScript notebook' },
      values: {},
      assets,
      definitions: {},
      stages,
      outputs: selectedOutputs.map((stage) => ({ stage })),
    },
    projections: projections.sort((left, right) => left.from - right.from || left.to - right.to),
  };
}
