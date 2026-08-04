import * as z from 'zod';

export const RECIPE_FORMAT = 'bin-block-recipe/v1';
export const DEFAULT_RENDER_PROFILE = 'numeric-srgb/v1';

const IdentifierSchema = z.string().regex(/^[a-z][a-z0-9-]*$/, 'Use lowercase kebab-case identifiers.');
const PropertyNameSchema = z.string().min(1);
const PathSchema = z.string().min(1);
const JsonSchema = z.json();
const PrimitiveSchema = z.union([z.string(), z.number().finite(), z.boolean(), z.null()]);

export const LiteralExpressionSchema = z.tuple([z.literal('literal'), JsonSchema]);

function variadicExpression(operator, minimumArguments = 1) {
  return z.tuple([z.literal(operator)], ExpressionValueSchema).refine(
    (expression) => expression.length >= minimumArguments + 1,
    `${operator} expects at least ${minimumArguments} argument${minimumArguments === 1 ? '' : 's'}.`,
  );
}

function binaryExpression(operator) {
  return z.tuple([z.literal(operator), ExpressionValueSchema, ExpressionValueSchema]);
}

export const ExpressionSchema = z.lazy(() => z.union([
  LiteralExpressionSchema,
  z.tuple([z.literal('var'), z.string().min(1)]),
  z.union([
    z.tuple([z.literal('get'), z.string().min(1)]),
    z.tuple([z.literal('get'), z.string().min(1), ExpressionValueSchema]),
  ]),
  variadicExpression('coalesce'),
  variadicExpression('concat'),
  variadicExpression('min'),
  variadicExpression('max'),
  binaryExpression('=='),
  binaryExpression('!='),
  binaryExpression('<'),
  binaryExpression('<='),
  binaryExpression('>'),
  binaryExpression('>='),
  binaryExpression('+'),
  binaryExpression('-'),
  binaryExpression('*'),
  binaryExpression('/'),
  z.tuple([z.literal('clamp'), ExpressionValueSchema, ExpressionValueSchema, ExpressionValueSchema]),
  z.tuple([z.literal('to-string'), ExpressionValueSchema]),
  z.tuple([z.literal('pad'), ExpressionValueSchema, ExpressionValueSchema, ExpressionValueSchema.optional()]),
  z.tuple([z.literal('case')], ExpressionValueSchema).refine(
    (expression) => expression.length >= 4 && expression.length % 2 === 0,
    'case expects condition/output pairs followed by a fallback.',
  ),
  z.tuple([z.literal('match')], ExpressionValueSchema).refine(
    (expression) => expression.length >= 5 && expression.length % 2 === 0,
    'match expects an input, label/output pairs, and a fallback.',
  ),
  z.tuple([z.literal('let')], ExpressionValueSchema).superRefine((expression, context) => {
    if (expression.length < 4 || expression.length % 2 !== 0) {
      context.addIssue({ code: 'custom', message: 'let expects name/value pairs followed by a result.' });
      return;
    }
    for (let index = 1; index < expression.length - 1; index += 2) {
      if (typeof expression[index] !== 'string') context.addIssue({ code: 'custom', path: [index], message: 'let binding names must be strings.' });
    }
  }),
]));

export const ExpressionValueSchema = z.lazy(() => z.union([PrimitiveSchema, ExpressionSchema]));
export const NumberValueSchema = z.union([z.number().finite(), ExpressionSchema]);
export const IntegerValueSchema = z.union([z.number().int(), ExpressionSchema]);
export const StringValueSchema = z.union([z.string(), ExpressionSchema]);
export const ColorValueSchema = z.union([
  z.string().regex(/^#[\da-f]{6}(?:[\da-f]{2})?$/i, 'Expected #rrggbb or #rrggbbaa.'),
  ExpressionSchema,
]);

export const RasterAssetSchema = z.strictObject({
  type: z.literal('raster'),
  path: PathSchema,
  sha256: z.string().regex(/^sha256:[\da-f]{64}$/i).optional(),
  width: z.number().int().positive().optional(),
  height: z.number().int().positive().optional(),
});

export const FontAssetSchema = z.strictObject({
  type: z.literal('font'),
  path: PathSchema,
  sha256: z.string().regex(/^sha256:[\da-f]{64}$/i).optional(),
  face: z.number().int().nonnegative().default(0),
});

export const AssetSchema = z.discriminatedUnion('type', [RasterAssetSchema, FontAssetSchema]);

const GradientStopSchema = z.strictObject({
  offset: NumberValueSchema,
  color: ColorValueSchema,
  easing: z.enum(['linear', 'smoothstep', 'legacy']).optional(),
});

export const ImageNodeSchema = z.lazy(() => z.union([
  z.strictObject({ op: z.literal('input'), binding: IdentifierSchema }),
  z.strictObject({ op: z.literal('use'), definition: IdentifierSchema, with: z.record(PropertyNameSchema, ExpressionValueSchema).default({}) }),
  z.strictObject({ op: z.literal('fill'), width: IntegerValueSchema, height: IntegerValueSchema, color: ColorValueSchema }),
  z.strictObject({
    op: z.literal('gradient'),
    shape: z.literal('preset'),
    width: IntegerValueSchema,
    height: IntegerValueSchema,
    preset: StringValueSchema,
    rotation: IntegerValueSchema.default(0),
    color: ColorValueSchema.default('#000000'),
  }),
  z.strictObject({
    op: z.literal('gradient'),
    shape: z.literal('ellipse'),
    width: IntegerValueSchema,
    height: IntegerValueSchema,
    centerX: NumberValueSchema.optional(),
    centerY: NumberValueSchema.optional(),
    radiusX: NumberValueSchema,
    radiusY: NumberValueSchema,
    rotation: NumberValueSchema.default(0),
    easing: z.enum(['linear', 'smoothstep', 'legacy']).default('linear'),
    stops: z.array(GradientStopSchema).min(2),
  }),
  z.strictObject({ op: z.literal('raster'), asset: StringValueSchema }),
  z.strictObject({
    op: z.literal('reference-set/alpha-map'),
    index: IntegerValueSchema,
    rasterAsset: StringValueSchema.optional(),
  }),
  z.strictObject({
    op: z.literal('glyph'),
    font: StringValueSchema,
    text: StringValueSchema,
    width: IntegerValueSchema,
    height: IntegerValueSchema,
    fontSize: NumberValueSchema,
    color: ColorValueSchema.default('#000000'),
    antialias: z.enum(['none', 'grayscale']).default('grayscale'),
    offsetX: NumberValueSchema.default(0),
    offsetY: NumberValueSchema.default(0),
  }),
  z.strictObject({
    op: z.literal('composite'),
    destination: ImageNodeSchema,
    source: ImageNodeSchema,
    offsetX: IntegerValueSchema.default(0),
    offsetY: IntegerValueSchema.default(0),
    opacity: NumberValueSchema.default(1),
  }),
  z.strictObject({ op: z.literal('opacity'), source: ImageNodeSchema, opacity: NumberValueSchema }),
  z.strictObject({ op: z.literal('set-visible-rgb'), source: ImageNodeSchema, color: ColorValueSchema }),
  z.strictObject({ op: z.literal('tint-chroma'), source: ImageNodeSchema, color: ColorValueSchema }),
  z.strictObject({
    op: z.literal('remap-two-color'),
    source: ImageNodeSchema,
    sourceForeground: ColorValueSchema,
    sourceBackground: ColorValueSchema,
    foreground: ColorValueSchema,
    background: ColorValueSchema,
  }),
  z.strictObject({
    op: z.literal('shift-rgb'),
    source: ImageNodeSchema,
    sourceBase: ColorValueSchema,
    targetBase: ColorValueSchema,
  }),
  z.strictObject({
    op: z.literal('apply-mask'),
    source: ImageNodeSchema,
    mask: ImageNodeSchema,
    mode: z.enum(['multiply', 'replace']).default('multiply'),
  }),
  z.strictObject({ op: z.literal('rotate'), source: ImageNodeSchema, turns: IntegerValueSchema }),
  z.strictObject({
    op: z.literal('resize'),
    source: ImageNodeSchema,
    width: IntegerValueSchema,
    height: IntegerValueSchema,
    filter: z.literal('lanczos3').default('lanczos3'),
  }),
  z.strictObject({
    op: z.literal('crop'),
    source: ImageNodeSchema,
    x: IntegerValueSchema,
    y: IntegerValueSchema,
    width: IntegerValueSchema,
    height: IntegerValueSchema,
  }),
]));

export const DefinitionSchema = z.strictObject({
  parameters: z.record(PropertyNameSchema, JsonSchema).default({}),
  image: ImageNodeSchema,
});

export const ValuesAxisSchema = z.strictObject({
  source: z.literal('values'),
  values: z.array(JsonSchema),
  where: ExpressionValueSchema.optional(),
});

export const StageAxisSchema = z.strictObject({
  source: z.literal('stage'),
  stage: IdentifierSchema,
  where: ExpressionValueSchema.optional(),
});

export const AxisSchema = z.discriminatedUnion('source', [ValuesAxisSchema, StageAxisSchema]);
const ExpansionSchema = z.record(IdentifierSchema, AxisSchema).default({});
const StageFields = {
  id: IdentifierSchema,
  forEach: ExpansionSchema,
  key: StringValueSchema,
  path: StringValueSchema.optional(),
  properties: z.record(PropertyNameSchema, ExpressionValueSchema).default({}),
};

export const RenderStageSchema = z.strictObject({
  type: z.literal('render'),
  ...StageFields,
  image: ImageNodeSchema,
});

export const AliasStageSchema = z.strictObject({
  type: z.literal('alias'),
  ...StageFields,
  target: z.strictObject({ binding: IdentifierSchema }),
  identity: z.enum(['bytes', 'pixels', 'recipe']).default('pixels'),
});

export const StageSchema = z.discriminatedUnion('type', [RenderStageSchema, AliasStageSchema]);
export const OutputSchema = z.strictObject({ stage: IdentifierSchema });

function visitImage(node, visitor) {
  visitor(node);
  if (node.op === 'composite') {
    visitImage(node.destination, visitor);
    visitImage(node.source, visitor);
    return;
  }
  if (node.source) visitImage(node.source, visitor);
  if (node.op === 'apply-mask') visitImage(node.mask, visitor);
}

function validateRecipeReferences(document, context) {
  const definitionDependencies = new Map(Object.keys(document.definitions).map((id) => [id, []]));
  for (const [definitionId, definition] of Object.entries(document.definitions)) visitImage(definition.image, (node) => {
    if (node.op === 'use') {
      definitionDependencies.get(definitionId).push(node.definition);
      if (!(node.definition in document.definitions)) context.addIssue({ code: 'custom', path: ['definitions', definitionId, 'image'], message: `Unknown definition: ${node.definition}` });
    }
    if (node.op === 'raster' && typeof node.asset === 'string' && document.assets[node.asset]?.type !== 'raster') context.addIssue({ code: 'custom', path: ['definitions', definitionId, 'image'], message: `Unknown raster asset: ${node.asset}` });
    if (node.op === 'glyph' && typeof node.font === 'string' && document.assets[node.font]?.type !== 'font') context.addIssue({ code: 'custom', path: ['definitions', definitionId, 'image'], message: `Unknown font asset: ${node.font}` });
  });

  const stages = new Map();
  document.stages.forEach((stage, index) => {
    if (stages.has(stage.id)) context.addIssue({ code: 'custom', path: ['stages', index, 'id'], message: `Duplicate stage id: ${stage.id}` });
    stages.set(stage.id, stage);
  });

  const dependencies = new Map(document.stages.map((stage) => [stage.id, []]));
  document.stages.forEach((stage, stageIndex) => {
    const bindings = new Set(Object.keys(stage.forEach));
    for (const [binding, axis] of Object.entries(stage.forEach)) {
      if (axis.source !== 'stage') continue;
      dependencies.get(stage.id).push(axis.stage);
      if (!stages.has(axis.stage)) context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'forEach', binding, 'stage'], message: `Unknown stage: ${axis.stage}` });
    }
    if (stage.type === 'alias' && !bindings.has(stage.target.binding)) {
      context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'target', 'binding'], message: `Unknown binding: ${stage.target.binding}` });
    }
    if (stage.type === 'render') visitImage(stage.image, (node) => {
      if (node.op === 'input' && !bindings.has(node.binding)) context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'image'], message: `Unknown input binding: ${node.binding}` });
      if (node.op === 'use' && !(node.definition in document.definitions)) context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'image'], message: `Unknown definition: ${node.definition}` });
      if (node.op === 'raster' && typeof node.asset === 'string' && document.assets[node.asset]?.type !== 'raster') context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'image'], message: `Unknown raster asset: ${node.asset}` });
      if (node.op === 'glyph' && typeof node.font === 'string' && document.assets[node.font]?.type !== 'font') context.addIssue({ code: 'custom', path: ['stages', stageIndex, 'image'], message: `Unknown font asset: ${node.font}` });
    });
  });

  for (const [outputIndex, output] of document.outputs.entries()) {
    if (!stages.has(output.stage)) context.addIssue({ code: 'custom', path: ['outputs', outputIndex, 'stage'], message: `Unknown output stage: ${output.stage}` });
  }

  const visiting = new Set();
  const visited = new Set();
  function visitStage(stageId, path = []) {
    if (visiting.has(stageId)) {
      context.addIssue({ code: 'custom', path: ['stages'], message: `Stage dependency cycle: ${[...path, stageId].join(' -> ')}` });
      return;
    }
    if (visited.has(stageId) || !dependencies.has(stageId)) return;
    visiting.add(stageId);
    for (const dependency of dependencies.get(stageId)) visitStage(dependency, [...path, stageId]);
    visiting.delete(stageId);
    visited.add(stageId);
  }
  for (const stageId of dependencies.keys()) visitStage(stageId);

  const visitingDefinitions = new Set();
  const visitedDefinitions = new Set();
  function visitDefinition(definitionId, path = []) {
    if (visitingDefinitions.has(definitionId)) {
      context.addIssue({ code: 'custom', path: ['definitions'], message: `Definition cycle: ${[...path, definitionId].join(' -> ')}` });
      return;
    }
    if (visitedDefinitions.has(definitionId) || !definitionDependencies.has(definitionId)) return;
    visitingDefinitions.add(definitionId);
    for (const dependency of definitionDependencies.get(definitionId)) visitDefinition(dependency, [...path, definitionId]);
    visitingDefinitions.delete(definitionId);
    visitedDefinitions.add(definitionId);
  }
  for (const definitionId of definitionDependencies.keys()) visitDefinition(definitionId);
}

export const RecipeDocumentSchema = z.strictObject({
  format: z.literal(RECIPE_FORMAT),
  profile: z.string().min(1).default(DEFAULT_RENDER_PROFILE),
  metadata: z.record(z.string(), JsonSchema).default({}),
  values: z.record(IdentifierSchema, JsonSchema).default({}),
  assets: z.record(IdentifierSchema, AssetSchema).default({}),
  definitions: z.record(IdentifierSchema, DefinitionSchema).default({}),
  stages: z.array(StageSchema).min(1),
  outputs: z.array(OutputSchema).min(1),
}).superRefine(validateRecipeReferences);

export function parseRecipeDocument(input) {
  return RecipeDocumentSchema.parse(input);
}

export function recipeJsonSchema() {
  return z.toJSONSchema(RecipeDocumentSchema, {
    target: 'draft-2020-12',
    cycles: 'ref',
    reused: 'ref',
  });
}
