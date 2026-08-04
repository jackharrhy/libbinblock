import { renderBlock, renderMask } from './core.js';
import {
  compositeSourceOver,
  fillRGBA,
  renderEllipticalGradient,
  resizeLanczos3RGBA,
  rotateRGBA90,
} from './legacy.js';
import { evaluateExpressionValue } from './recipe-expressions.js';
import { expandStageBindings, sortRecipeStages } from './recipe-graph.js';
import { parseRecipeDocument } from './recipe-schema.js';

function assertImage(image, label = 'image') {
  if (!image || !(image.pixels instanceof Uint8ClampedArray) || !Number.isInteger(image.width) || !Number.isInteger(image.height)) {
    throw new Error(`${label} must contain Uint8ClampedArray pixels and integer dimensions.`);
  }
  if (image.width < 1 || image.height < 1 || image.pixels.length !== image.width * image.height * 4) {
    throw new Error(`${label} has inconsistent dimensions.`);
  }
  return image;
}

function evaluate(nodeValue, context) {
  return evaluateExpressionValue(nodeValue, { variables: context.variables, properties: context.values });
}

function integer(nodeValue, context, label, minimum = Number.MIN_SAFE_INTEGER) {
  const value = evaluate(nodeValue, context);
  if (!Number.isInteger(value) || value < minimum) throw new Error(`${label} must be an integer${minimum > Number.MIN_SAFE_INTEGER ? ` of at least ${minimum}` : ''}.`);
  return value;
}

function number(nodeValue, context, label, minimum = -Infinity, maximum = Infinity) {
  const value = evaluate(nodeValue, context);
  if (typeof value !== 'number' || !Number.isFinite(value) || value < minimum || value > maximum) {
    throw new Error(`${label} must be a finite number between ${minimum} and ${maximum}.`);
  }
  return value;
}

function string(nodeValue, context, label) {
  const value = evaluate(nodeValue, context);
  if (typeof value !== 'string') throw new Error(`${label} must evaluate to a string.`);
  return value;
}

function rgb(value) {
  const match = value.match(/^#([\da-f]{2})([\da-f]{2})([\da-f]{2})(?:[\da-f]{2})?$/i);
  if (!match) throw new Error(`Invalid color: ${value}`);
  return match.slice(1, 4).map((channel) => Number.parseInt(channel, 16));
}

function expressionArtifact(artifact) {
  return { stage: artifact.stage, key: artifact.key, path: artifact.path, properties: artifact.properties };
}

function expressionVariables(values, bindings) {
  return Object.fromEntries(Object.entries(values).concat(Object.entries(bindings).map(([name, value]) => [
    name,
    value?.image ? expressionArtifact(value) : value,
  ])));
}

function validateOutputPath(path) {
  if (!path || path.startsWith('/') || path.split('/').includes('..')) throw new Error(`Output path must be relative and cannot contain '..': ${path}`);
  return path;
}

function cropImage(source, x, y, width, height) {
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let outputY = 0; outputY < height; outputY += 1) {
    const sourceY = y + outputY;
    if (sourceY < 0 || sourceY >= source.height) continue;
    for (let outputX = 0; outputX < width; outputX += 1) {
      const sourceX = x + outputX;
      if (sourceX < 0 || sourceX >= source.width) continue;
      const sourceOffset = (sourceY * source.width + sourceX) * 4;
      pixels.set(source.pixels.subarray(sourceOffset, sourceOffset + 4), (outputY * width + outputX) * 4);
    }
  }
  return { pixels, width, height };
}

export const BUILTIN_IMAGE_OPERATIONS = {
  input: async (node, context) => {
    const artifact = context.bindings[node.binding];
    if (!artifact?.image) throw new Error(`Input binding ${node.binding} does not contain an image artifact.`);
    return artifact.image;
  },
  use: async (node, context) => {
    const definition = context.document.definitions[node.definition];
    if (!definition) throw new Error(`Unknown definition: ${node.definition}`);
    if (context.definitionStack.includes(node.definition)) throw new Error(`Definition cycle: ${[...context.definitionStack, node.definition].join(' -> ')}`);
    const parameters = { ...definition.parameters };
    for (const [name, value] of Object.entries(node.with)) parameters[name] = evaluate(value, context);
    return context.execute(definition.image, {
      ...context,
      variables: { ...context.variables, ...parameters },
      definitionStack: [...context.definitionStack, node.definition],
    });
  },
  fill: async (node, context) => {
    const width = integer(node.width, context, 'fill width', 1);
    const height = integer(node.height, context, 'fill height', 1);
    return { width, height, pixels: fillRGBA(width, height, string(node.color, context, 'fill color')) };
  },
  gradient: async (node, context) => {
    const width = integer(node.width, context, 'gradient width', 1);
    const height = integer(node.height, context, 'gradient height', 1);
    if (node.shape === 'preset') {
      const pixels = renderMask({
        width,
        height,
        preset: string(node.preset, context, 'gradient preset'),
        rotation: integer(node.rotation, context, 'gradient rotation'),
      });
      const color = rgb(string(node.color, context, 'gradient color'));
      for (let offset = 0; offset < pixels.length; offset += 4) pixels.set(color, offset);
      return { width, height, pixels };
    }
    return {
      width,
      height,
      pixels: renderEllipticalGradient({
        width,
        height,
        centerX: node.centerX === undefined ? undefined : number(node.centerX, context, 'gradient centerX'),
        centerY: node.centerY === undefined ? undefined : number(node.centerY, context, 'gradient centerY'),
        radiusX: number(node.radiusX, context, 'gradient radiusX', Number.EPSILON),
        radiusY: number(node.radiusY, context, 'gradient radiusY', Number.EPSILON),
        rotation: number(node.rotation, context, 'gradient rotation'),
        easing: node.easing,
        stops: node.stops.map((stop) => ({
          offset: number(stop.offset, context, 'gradient stop offset', 0, 1),
          color: string(stop.color, context, 'gradient stop color'),
          easing: stop.easing,
        })),
      }),
    };
  },
  raster: async (node, context) => {
    const assetId = string(node.asset, context, 'raster asset');
    const asset = context.document.assets[assetId];
    if (asset?.type !== 'raster') throw new Error(`Unknown raster asset: ${assetId}`);
    if (!context.resolveRaster) throw new Error(`No raster resolver was provided for asset ${assetId}.`);
    return assertImage(await context.resolveRaster(assetId, asset), `raster ${assetId}`);
  },
  glyph: async (node, context) => {
    const fontId = string(node.font, context, 'glyph font');
    const asset = context.document.assets[fontId];
    if (asset?.type !== 'font') throw new Error(`Unknown font asset: ${fontId}`);
    if (!context.renderGlyph) throw new Error(`No glyph renderer was provided for font ${fontId}.`);
    return assertImage(await context.renderGlyph({
      font: fontId,
      asset,
      text: string(node.text, context, 'glyph text'),
      width: integer(node.width, context, 'glyph width', 1),
      height: integer(node.height, context, 'glyph height', 1),
      fontSize: number(node.fontSize, context, 'glyph font size', Number.EPSILON),
      color: string(node.color, context, 'glyph color'),
      antialias: node.antialias,
      offsetX: number(node.offsetX, context, 'glyph offsetX'),
      offsetY: number(node.offsetY, context, 'glyph offsetY'),
    }), 'glyph renderer result');
  },
  composite: async (node, context) => {
    const destination = await context.execute(node.destination, context);
    const source = await context.execute(node.source, context);
    return {
      width: destination.width,
      height: destination.height,
      pixels: compositeSourceOver(destination.pixels, destination.width, destination.height, source.pixels, source.width, source.height, {
        offsetX: integer(node.offsetX, context, 'composite offsetX'),
        offsetY: integer(node.offsetY, context, 'composite offsetY'),
        opacity: number(node.opacity, context, 'composite opacity', 0, 1),
      }),
    };
  },
  opacity: async (node, context) => {
    const source = await context.execute(node.source, context);
    const amount = number(node.opacity, context, 'opacity', 0, 1);
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 3; offset < pixels.length; offset += 4) pixels[offset] = Math.round(pixels[offset] * amount);
    return { ...source, pixels };
  },
  'set-visible-rgb': async (node, context) => {
    const source = await context.execute(node.source, context);
    const color = rgb(string(node.color, context, 'visible RGB color'));
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 0; offset < pixels.length; offset += 4) {
      if (pixels[offset + 3] !== 0) pixels.set(color, offset);
    }
    return { ...source, pixels };
  },
  'tint-chroma': async (node, context) => {
    const source = await context.execute(node.source, context);
    const color = rgb(string(node.color, context, 'chroma tint color'));
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 0; offset < pixels.length; offset += 4) {
      const minimum = Math.min(source.pixels[offset], source.pixels[offset + 1], source.pixels[offset + 2]);
      const chroma = Math.max(source.pixels[offset], source.pixels[offset + 1], source.pixels[offset + 2]) - minimum;
      for (let channel = 0; channel < 3; channel += 1) pixels[offset + channel] = minimum + chroma * color[channel] / 255;
    }
    return { ...source, pixels };
  },
  'remap-two-color': async (node, context) => {
    const source = await context.execute(node.source, context);
    const sourceForeground = rgb(string(node.sourceForeground, context, 'source foreground'));
    const sourceBackground = rgb(string(node.sourceBackground, context, 'source background'));
    const foreground = rgb(string(node.foreground, context, 'foreground'));
    const background = rgb(string(node.background, context, 'background'));
    const denominator = sourceForeground.reduce((total, channel, index) => total + (channel - sourceBackground[index]) ** 2, 0);
    if (denominator === 0) throw new Error('remap-two-color source colors must differ.');
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 0; offset < pixels.length; offset += 4) {
      let amount = 0;
      for (let channel = 0; channel < 3; channel += 1) amount += (source.pixels[offset + channel] - sourceBackground[channel]) * (sourceForeground[channel] - sourceBackground[channel]);
      amount = Math.min(1, Math.max(0, amount / denominator));
      for (let channel = 0; channel < 3; channel += 1) pixels[offset + channel] = background[channel] * (1 - amount) + foreground[channel] * amount;
    }
    return { ...source, pixels };
  },
  'shift-rgb': async (node, context) => {
    const source = await context.execute(node.source, context);
    const sourceBase = rgb(string(node.sourceBase, context, 'source base'));
    const targetBase = rgb(string(node.targetBase, context, 'target base'));
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 0; offset < pixels.length; offset += 4) {
      for (let channel = 0; channel < 3; channel += 1) pixels[offset + channel] = targetBase[channel] + source.pixels[offset + channel] - sourceBase[channel];
    }
    return { ...source, pixels };
  },
  'apply-mask': async (node, context) => {
    const source = await context.execute(node.source, context);
    const mask = await context.execute(node.mask, context);
    if (source.width !== mask.width || source.height !== mask.height) throw new Error('apply-mask inputs must have matching dimensions.');
    const pixels = new Uint8ClampedArray(source.pixels);
    for (let offset = 3; offset < pixels.length; offset += 4) {
      pixels[offset] = node.mode === 'replace'
        ? mask.pixels[offset]
        : Math.round(pixels[offset] * mask.pixels[offset] / 255);
    }
    return { ...source, pixels };
  },
  rotate: async (node, context) => {
    const source = await context.execute(node.source, context);
    return rotateRGBA90(source.pixels, source.width, source.height, integer(node.turns, context, 'rotation turns'));
  },
  resize: async (node, context) => {
    const source = await context.execute(node.source, context);
    const width = integer(node.width, context, 'resize width', 1);
    const height = integer(node.height, context, 'resize height', 1);
    return { width, height, pixels: resizeLanczos3RGBA(source.pixels, source.width, source.height, width, height) };
  },
  crop: async (node, context) => {
    const source = await context.execute(node.source, context);
    return cropImage(
      source,
      integer(node.x, context, 'crop x'),
      integer(node.y, context, 'crop y'),
      integer(node.width, context, 'crop width', 1),
      integer(node.height, context, 'crop height', 1),
    );
  },
};

function createImageExecutor(operations) {
  const registry = { ...BUILTIN_IMAGE_OPERATIONS, ...operations };
  const execute = async (node, context) => {
    const operation = registry[node.op];
    if (!operation) throw new Error(`No executor is registered for image operation ${node.op}.`);
    return assertImage(await operation(node, { ...context, execute }), `${node.op} result`);
  };
  return execute;
}

export async function compileRecipe(input, options = {}) {
  const document = parseRecipeDocument(input);
  const artifactsByStage = new Map();
  const execute = createImageExecutor(options.operations);
  const maximumArtifacts = options.maximumArtifacts ?? 100_000;
  let artifactCount = 0;

  for (const stage of sortRecipeStages(document.stages)) {
    const combinations = expandStageBindings(stage, artifactsByStage, (where, bindings) => Boolean(evaluateExpressionValue(where, {
      variables: expressionVariables(document.values, bindings),
      properties: document.values,
    })));
    const stageArtifacts = [];
    const keys = new Set();
    for (const bindings of combinations) {
      artifactCount += 1;
      if (artifactCount > maximumArtifacts) throw new Error(`Recipe exceeds the ${maximumArtifacts} artifact limit.`);
      const variables = expressionVariables(document.values, bindings);
      const context = {
        document,
        values: document.values,
        variables,
        bindings,
        definitionStack: [],
        resolveRaster: options.resolveRaster,
        renderGlyph: options.renderGlyph,
      };
      const key = string(stage.key, context, `stage ${stage.id} key`);
      if (keys.has(key)) throw new Error(`Stage ${stage.id} produced duplicate key ${key}.`);
      keys.add(key);
      const path = stage.path === undefined ? undefined : validateOutputPath(string(stage.path, context, `stage ${stage.id} path`));
      const properties = Object.fromEntries(Object.entries(stage.properties).map(([name, value]) => [name, evaluate(value, context)]));
      let image;
      let provenance;
      if (stage.type === 'render') {
        image = await execute(stage.image, context);
        provenance = { type: 'render', operation: stage.image.op };
      } else {
        const target = bindings[stage.target.binding];
        if (!target?.image) throw new Error(`Alias stage ${stage.id} target ${stage.target.binding} is not an image artifact.`);
        image = target.image;
        provenance = { type: 'alias', identity: stage.identity, target: `${target.stage}/${target.key}` };
      }
      stageArtifacts.push({ stage: stage.id, key, path, properties, image, provenance });
    }
    artifactsByStage.set(stage.id, stageArtifacts);
  }

  const outputs = document.outputs.flatMap(({ stage }) => artifactsByStage.get(stage) ?? []);
  const paths = new Set();
  for (const output of outputs) {
    if (!output.path) throw new Error(`Output ${output.stage}/${output.key} has no path.`);
    if (paths.has(output.path)) throw new Error(`Duplicate output path: ${output.path}`);
    paths.add(output.path);
  }
  return { document, stages: artifactsByStage, outputs };
}
