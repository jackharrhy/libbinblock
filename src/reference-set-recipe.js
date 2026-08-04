import { RECIPE_FORMAT } from './recipe-schema.js';
import { getRedPrintArchiveRecipe, getRedPrintRecipe, getResultAliasMapping, RED_FG_ALPHA_SOURCE_SPECS } from './legacy.js';
import { PRESETS } from './core.js';

export const REFERENCE_SET_RECIPE_FAMILIES = [
  { id: 'flat-color', sources: [{ prefix: 'col/', output: 'flat-color/' }] },
  { id: 'gradient-masks', sources: [{ prefix: 'Gradient Layers Alpha Maps/', output: 'gradient-masks/' }] },
  { id: 'gradient-variants', sources: [{ prefix: 'col bin 2/', output: 'gradient-variants/' }] },
  { id: 'downscaled', sources: [{ prefix: 'blue 64-8 24 bit/', output: 'downscaled/' }] },
  { id: 'foreground-alpha', sources: [{ prefix: 'red FG-Alpha/', output: 'foreground-alpha/' }] },
  { id: 'foreground-composites', sources: [{ prefix: 'Red-col fg-alpha Print/print output/', output: 'foreground-composites/' }] },
  { id: 'elliptical-gradients', sources: [{ prefix: 'brown bear/', output: 'elliptical-gradients/' }] },
  {
    id: 'layer-compositions',
    sources: [
      { prefix: 'out4 - modding/', output: 'layer-compositions/variants/' },
      { prefix: 'out4 - Select Library/', output: 'layer-compositions/curated/' },
      { prefix: 'out4-special/', output: 'layer-compositions/special/' },
    ],
  },
  { id: 'sans-glyphs', sources: [{ prefix: 'No AA 64px Black+White/', output: 'glyphs/sans-serif/' }] },
  { id: 'serif-glyphs', sources: [{ prefix: 'New folder/', output: 'glyphs/serif/' }] },
  { id: 'ordered-results', sources: [{ prefix: 'result/', output: 'ordered-results/' }] },
];

function familyFile(families, path) {
  for (const family of families) {
    for (const source of family.sources) {
      if (path.startsWith(source.prefix)) return {
        family: family.id,
        path: `${source.output}${path.slice(source.prefix.length)}`,
      };
    }
  }
  return undefined;
}

export function createReferenceSetRasterRecipe(archive, { families = REFERENCE_SET_RECIPE_FAMILIES } = {}) {
  const assets = {};
  const filesByFamily = new Map(families.map((family) => [family.id, []]));
  archive.files.forEach((file, index) => {
    const mapped = familyFile(families, file.path);
    if (!mapped) throw new Error(`No reference-set recipe family maps archive path: ${file.path}`);
    const asset = `reference-set-raster-${String(index).padStart(4, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    filesByFamily.get(mapped.family).push({ asset, key: file.path, path: mapped.path, sourcePath: file.path });
  });

  const stages = families
    .filter((family) => filesByFamily.get(family.id).length)
    .map((family) => ({
      type: 'render',
      id: family.id,
      forEach: { file: { source: 'values', values: filesByFamily.get(family.id) } },
      key: ['get', 'key', ['var', 'file']],
      path: ['get', 'path', ['var', 'file']],
      properties: {
        sourcePath: ['get', 'sourcePath', ['var', 'file']],
        reproduction: 'raster-exact',
      },
      image: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
    }));

  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: {
      name: 'Reference set',
      description: 'A raster-backed recipe for the complete reference set, grouped by family.',
      imageCount: archive.files.length,
    },
    assets,
    stages,
    outputs: stages.map((stage) => ({ stage: stage.id })),
  };
}

export function createFlatColorRecipe(palette) {
  return {
    format: RECIPE_FORMAT,
    metadata: { name: 'Flat colors', imageCount: palette.length },
    stages: [{
      type: 'render',
      id: 'flat-color',
      forEach: {
        color: {
          source: 'values',
          values: palette.map(([id, value]) => ({ id, value })),
        },
      },
      key: ['get', 'id', ['var', 'color']],
      path: ['concat', 'flat-color/', ['get', 'id', ['var', 'color']], '.png'],
      properties: {
        color: ['get', 'value', ['var', 'color']],
        reproduction: 'analytic',
      },
      image: {
        op: 'fill',
        width: 64,
        height: 64,
        color: ['get', 'value', ['var', 'color']],
      },
    }],
    outputs: [{ stage: 'flat-color' }],
  };
}

export function createDownscaledRecipe(archive, { color = '#0000ff', size = 8 } = {}) {
  const files = archive.files.filter((file) => file.path.startsWith('blue 64-8 24 bit/'));
  const assets = {};
  const values = files.map((file, index) => {
    const asset = `downscaled-source-${String(index).padStart(3, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    return { asset, filename: file.path.slice('blue 64-8 24 bit/'.length) };
  });
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: 'Downscaled studies', imageCount: files.length },
    values: { color, size },
    assets,
    stages: [{
      type: 'render',
      id: 'downscaled',
      forEach: { file: { source: 'values', values } },
      key: ['get', 'filename', ['var', 'file']],
      path: ['concat', 'downscaled/', ['get', 'filename', ['var', 'file']]],
      properties: {
        color: ['var', 'color'],
        size: ['var', 'size'],
        reproduction: 'analytic-from-pinned-source',
      },
      image: {
        op: 'resize',
        source: {
          op: 'tint-chroma',
          source: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
          color: ['var', 'color'],
        },
        width: ['var', 'size'],
        height: ['var', 'size'],
      },
    }],
    outputs: [{ stage: 'downscaled' }],
  };
}

export function createGradientMaskRecipe(archive) {
  const map18 = archive.files.find((file) => file.path === 'Gradient Layers Alpha Maps/18.png');
  if (!map18) throw new Error('The reference set is missing Gradient Layers Alpha Maps/18.png.');
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: 'Gradient masks', imageCount: 19 },
    assets: {
      'gradient-map-18': { type: 'raster', path: map18.path, ...(map18.sha256 ? { sha256: map18.sha256 } : {}) },
    },
    stages: [{
      type: 'render',
      id: 'gradient-masks',
      forEach: {
        map: {
          source: 'values',
          values: Array.from({ length: 19 }, (_, index) => ({ index, name: String(index).padStart(2, '0') })),
        },
      },
      key: ['get', 'name', ['var', 'map']],
      path: ['concat', 'gradient-masks/', ['get', 'name', ['var', 'map']], '.png'],
      properties: {
        index: ['get', 'index', ['var', 'map']],
        reproduction: ['case', ['==', ['get', 'index', ['var', 'map']], 18], 'raster-exact', 'analytic-alpha-exact'],
      },
      image: {
        op: 'reference-set/alpha-map',
        index: ['get', 'index', ['var', 'map']],
        rasterAsset: 'gradient-map-18',
      },
    }],
    outputs: [{ stage: 'gradient-masks' }],
  };
}

export function createForegroundAlphaRecipe(archive, { color = '#ff0000' } = {}) {
  const files = archive.files.filter((file) => file.path.startsWith('red FG-Alpha/'));
  const assets = {};
  const values = files.map((file, index) => {
    const asset = `foreground-field-${String(index + 1).padStart(2, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    return { asset, filename: file.path.slice('red FG-Alpha/'.length) };
  });
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: 'Foreground alpha', imageCount: files.length },
    values: { color },
    assets,
    stages: [
      {
        type: 'render',
        id: 'foreground-alpha-fields',
        forEach: { file: { source: 'values', values } },
        key: ['get', 'filename', ['var', 'file']],
        properties: { sourcePath: ['get', 'filename', ['var', 'file']] },
        image: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
      },
      {
        type: 'render',
        id: 'foreground-alpha',
        forEach: { field: { source: 'stage', stage: 'foreground-alpha-fields' } },
        key: ['get', 'key', ['var', 'field']],
        path: ['concat', 'foreground-alpha/', ['get', 'key', ['var', 'field']]],
        properties: {
          color: ['var', 'color'],
          reproduction: color === '#ff0000' ? 'raster-exact' : 'analytic-from-pinned-field',
        },
        image: color === '#ff0000'
          ? { op: 'input', binding: 'field' }
          : {
            op: 'tint-chroma',
            source: { op: 'input', binding: 'field' },
            color: ['var', 'color'],
          },
      },
    ],
    outputs: [{ stage: 'foreground-alpha' }],
  };
}

export function createRasterFamilyRecipe(archive, familyId) {
  const family = REFERENCE_SET_RECIPE_FAMILIES.find((item) => item.id === familyId);
  if (!family) throw new Error(`Unknown reference-set recipe family: ${familyId}`);
  const assets = {};
  const values = [];
  for (const [index, file] of archive.files.entries()) {
    const mapped = familyFile([family], file.path);
    if (!mapped) continue;
    const asset = `${familyId}-raster-${String(index).padStart(4, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    values.push({ asset, key: file.path, path: mapped.path });
  }
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: familyId, imageCount: values.length },
    assets,
    stages: [{
      type: 'render',
      id: familyId,
      forEach: { file: { source: 'values', values } },
      key: ['get', 'key', ['var', 'file']],
      path: ['get', 'path', ['var', 'file']],
      properties: { reproduction: 'raster-exact' },
      image: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
    }],
    outputs: [{ stage: familyId }],
  };
}

export function createTwoColorFamilyRecipe(archive, {
  familyId,
  sourceForeground,
  sourceBackground,
  foreground,
  background,
}) {
  const recipe = createRasterFamilyRecipe(archive, familyId);
  recipe.values = { foreground, background };
  recipe.stages[0].properties = { foreground: ['var', 'foreground'], background: ['var', 'background'], reproduction: 'transformed-raster' };
  recipe.stages[0].image = {
    op: 'remap-two-color',
    source: recipe.stages[0].image,
    sourceForeground,
    sourceBackground,
    foreground: ['var', 'foreground'],
    background: ['var', 'background'],
  };
  return recipe;
}

export function createOrganicGradientRecipe(archive, { color = '#993300' } = {}) {
  const recipe = createRasterFamilyRecipe(archive, 'elliptical-gradients');
  recipe.values = { color };
  recipe.stages[0].properties = { color: ['var', 'color'], reproduction: color === '#993300' ? 'raster-exact' : 'transformed-raster' };
  if (color !== '#993300') recipe.stages[0].image = {
    op: 'shift-rgb',
    source: recipe.stages[0].image,
    sourceBase: '#993300',
    targetBase: ['var', 'color'],
  };
  return recipe;
}

export function createForegroundCompositeRecipe(archive, { color = '#ff0000' } = {}) {
  const sourceFiles = archive.files.filter((file) => file.path.startsWith('red FG-Alpha/'));
  const outputFiles = archive.files.filter((file) => file.path.startsWith('Red-col fg-alpha Print/print output/'));
  const assets = {};
  const fields = sourceFiles.map((file, index) => {
    const asset = `red-field-${String(index + 1).padStart(2, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    return { asset, key: file.path.slice('red FG-Alpha/'.length) };
  });
  const outputs = [];
  let flatOutput;
  for (const [index, file] of outputFiles.entries()) {
    const archiveRecipe = getRedPrintArchiveRecipe(file.path);
    const asset = `red-output-${String(index).padStart(3, '0')}`;
    assets[asset] = { type: 'raster', path: file.path, ...(file.sha256 ? { sha256: file.sha256 } : {}) };
    const filename = file.path.split('/').at(-1);
    if (archiveRecipe.kind === 'flat-alias') {
      flatOutput = { asset, filename, color: archiveRecipe.colour };
      continue;
    }
    const layer = getRedPrintRecipe(archiveRecipe.layerNumber);
    const spec = RED_FG_ALPHA_SOURCE_SPECS[layer.sourceNumber];
    outputs.push({
      asset,
      filename,
      fieldKey: `${layer.sourceNumber}.png`,
      base: layer.base,
      offsetX: spec.offsetX,
      offsetY: spec.offsetY,
    });
  }
  const transformed = color !== '#ff0000';
  const stages = [
    {
      type: 'render',
      id: 'foreground-composite-fields',
      forEach: { file: { source: 'values', values: fields } },
      key: ['get', 'key', ['var', 'file']],
      image: transformed
        ? { op: 'set-visible-rgb', source: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] }, color: ['var', 'color'] }
        : { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
    },
    {
      type: 'render',
      id: 'foreground-composites',
      forEach: {
        file: { source: 'values', values: outputs },
        field: {
          source: 'stage',
          stage: 'foreground-composite-fields',
          where: ['==', ['get', 'key', ['var', 'field']], ['get', 'fieldKey', ['var', 'file']]],
        },
      },
      key: ['get', 'filename', ['var', 'file']],
      path: ['concat', 'foreground-composites/', ['get', 'filename', ['var', 'file']]],
      properties: { color: ['var', 'color'], reproduction: transformed ? 'generated' : 'raster-exact' },
      image: transformed
        ? {
          op: 'composite',
          destination: { op: 'fill', width: 64, height: 64, color: ['get', 'base', ['var', 'file']] },
          source: { op: 'input', binding: 'field' },
          offsetX: ['get', 'offsetX', ['var', 'file']],
          offsetY: ['get', 'offsetY', ['var', 'file']],
        }
        : { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
    },
  ];
  if (flatOutput) stages.push({
    type: 'render',
    id: 'foreground-composite-flat',
    forEach: { file: { source: 'values', values: [flatOutput] } },
    key: ['get', 'filename', ['var', 'file']],
    path: ['concat', 'foreground-composites/', ['get', 'filename', ['var', 'file']]],
    properties: { reproduction: transformed ? 'analytic-exact' : 'raster-exact' },
    image: transformed
      ? { op: 'fill', width: 64, height: 64, color: ['get', 'color', ['var', 'file']] }
      : { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
  });
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: 'Foreground composites', imageCount: outputFiles.length },
    values: { color },
    assets,
    stages,
    outputs: stages.filter((stage) => stage.id !== 'foreground-composite-fields').map((stage) => ({ stage: stage.id })),
  };
}

export function createOrderedResultsRecipe(archive) {
  const filesByPath = new Map(archive.files.map((file) => [file.path, file]));
  const assets = {};
  const sources = [];
  const aliases = [];
  const exceptions = [];
  for (let index = 0; index < 972; index += 1) {
    const mapping = getResultAliasMapping(index);
    const resultFile = filesByPath.get(mapping.resultPath);
    if (!resultFile) throw new Error(`The reference set is missing ${mapping.resultPath}.`);
    const resultFilename = mapping.resultPath.split('/').at(-1);
    if (!mapping.exactPixelAliasExpected) {
      const asset = `ordered-exception-${String(index).padStart(4, '0')}`;
      assets[asset] = { type: 'raster', path: resultFile.path, ...(resultFile.sha256 ? { sha256: resultFile.sha256 } : {}) };
      exceptions.push({ asset, key: resultFilename });
      continue;
    }
    const sourceFile = filesByPath.get(mapping.rgbPath);
    if (!sourceFile) throw new Error(`The reference set is missing alias source ${mapping.rgbPath}.`);
    const sourceKey = mapping.rgbPath;
    if (!sources.some((source) => source.key === sourceKey)) {
      const asset = `ordered-source-${String(sources.length).padStart(4, '0')}`;
      assets[asset] = { type: 'raster', path: sourceFile.path, ...(sourceFile.sha256 ? { sha256: sourceFile.sha256 } : {}) };
      sources.push({ asset, key: sourceKey });
    }
    aliases.push({ sourceKey, key: resultFilename });
  }
  return {
    format: RECIPE_FORMAT,
    profile: 'reference-set-exact/v1',
    metadata: { name: 'Ordered results', imageCount: 972, pixelAliases: aliases.length, rasterExceptions: exceptions.length },
    assets,
    stages: [
      {
        type: 'render',
        id: 'ordered-result-sources',
        forEach: { file: { source: 'values', values: sources } },
        key: ['get', 'key', ['var', 'file']],
        image: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
      },
      {
        type: 'alias',
        id: 'ordered-results',
        forEach: {
          alias: { source: 'values', values: aliases },
          target: {
            source: 'stage',
            stage: 'ordered-result-sources',
            where: ['==', ['get', 'key', ['var', 'target']], ['get', 'sourceKey', ['var', 'alias']]],
          },
        },
        target: { binding: 'target' },
        identity: 'pixels',
        key: ['get', 'key', ['var', 'alias']],
        path: ['concat', 'ordered-results/', ['get', 'key', ['var', 'alias']]],
        properties: { reproduction: 'pixel-alias' },
      },
      {
        type: 'render',
        id: 'ordered-result-exceptions',
        forEach: { file: { source: 'values', values: exceptions } },
        key: ['get', 'key', ['var', 'file']],
        path: ['concat', 'ordered-results/', ['get', 'key', ['var', 'file']]],
        properties: { reproduction: 'raster-exact', exception: 'missing-grad06blk50-rgb-export' },
        image: { op: 'raster', asset: ['get', 'asset', ['var', 'file']] },
      },
    ],
    outputs: [{ stage: 'ordered-results' }, { stage: 'ordered-result-exceptions' }],
  };
}

export function createCustomPermutationRecipe(palette, presetNames, overlays) {
  const colors = palette.map(([id, value]) => ({ id, value }));
  const presets = presetNames.map((id) => ({ id, archiveName: PRESETS[id]?.archiveName ?? id }));
  const variants = [];
  let index = 0;
  for (const color of colors) {
    for (const preset of presets) {
      for (const overlay of overlays) {
        variants.push({ index, color, preset, overlay });
        index += 1;
      }
    }
  }
  return {
    format: RECIPE_FORMAT,
    metadata: { name: 'Custom gradient permutations', imageCount: colors.length + presets.length + variants.length * 2 },
    stages: [
      {
        type: 'render',
        id: 'custom-flat',
        forEach: { color: { source: 'values', values: colors } },
        key: ['get', 'id', ['var', 'color']],
        path: ['concat', 'generated/flat/', ['get', 'id', ['var', 'color']], '.png'],
        properties: { base: ['get', 'value', ['var', 'color']] },
        image: { op: 'fill', width: 64, height: 64, color: ['get', 'value', ['var', 'color']] },
      },
      {
        type: 'render',
        id: 'custom-masks',
        forEach: { preset: { source: 'values', values: presets } },
        key: ['get', 'id', ['var', 'preset']],
        path: ['concat', 'generated/masks/', ['get', 'archiveName', ['var', 'preset']], '.png'],
        properties: { preset: ['get', 'id', ['var', 'preset']] },
        image: { op: 'gradient', shape: 'preset', width: 64, height: 64, preset: ['get', 'id', ['var', 'preset']] },
      },
      {
        type: 'render',
        id: 'custom-variants',
        forEach: { variant: { source: 'values', values: variants } },
        key: ['concat',
          ['get', 'id', ['get', 'color', ['var', 'variant']]], '-',
          ['get', 'archiveName', ['get', 'preset', ['var', 'variant']]],
          ['get', 'name', ['get', 'overlay', ['var', 'variant']]],
        ],
        path: ['concat', 'generated/variants/',
          ['get', 'id', ['get', 'color', ['var', 'variant']]], '-',
          ['get', 'archiveName', ['get', 'preset', ['var', 'variant']]],
          ['get', 'name', ['get', 'overlay', ['var', 'variant']]], '.png',
        ],
        properties: { index: ['get', 'index', ['var', 'variant']] },
        image: {
          op: 'composite',
          destination: { op: 'fill', width: 64, height: 64, color: ['get', 'value', ['get', 'color', ['var', 'variant']]] },
          source: {
            op: 'gradient',
            shape: 'preset',
            width: 64,
            height: 64,
            preset: ['get', 'id', ['get', 'preset', ['var', 'variant']]],
            color: ['get', 'colour', ['get', 'overlay', ['var', 'variant']]],
          },
        },
      },
      {
        type: 'alias',
        id: 'custom-results',
        forEach: { target: { source: 'stage', stage: 'custom-variants' } },
        target: { binding: 'target' },
        identity: 'recipe',
        key: ['concat', 'Generated_', ['pad', ['to-string', ['get', 'index', ['get', 'properties', ['var', 'target']]]], 4]],
        path: ['concat', 'generated/results/Generated_', ['pad', ['to-string', ['get', 'index', ['get', 'properties', ['var', 'target']]]], 4], '.png'],
        properties: { reproduction: 'recipe-alias' },
      },
    ],
    outputs: [
      { stage: 'custom-flat' },
      { stage: 'custom-masks' },
      { stage: 'custom-variants' },
      { stage: 'custom-results' },
    ],
  };
}
