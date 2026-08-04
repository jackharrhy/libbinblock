import JSZip from 'jszip';
import './style.css';
import { DEFAULT_PALETTE, PRESETS } from './core.js';
import { REFERENCE_SET_IMAGE_OPERATIONS } from './reference-set-operations.js';
import {
  createDownscaledRecipe,
  createCustomPermutationRecipe,
  createFlatColorRecipe,
  createForegroundAlphaRecipe,
  createForegroundCompositeRecipe,
  createGradientMaskRecipe,
  createOrganicGradientRecipe,
  createOrderedResultsRecipe,
  createRasterFamilyRecipe,
  createTwoColorFamilyRecipe,
} from './reference-set-recipe.js';
import { compileRecipe } from './recipe-executor.js';
import { parseRecipeDocument, recipeJsonSchema } from './recipe-schema.js';
import { REFERENCE_SET_ARCHIVE } from 'virtual:reference-set-archive';

const overlays = [
  { name: 'blk100', colour: '#000000' },
  { name: 'wht100', colour: '#ffffff' },
];
const archiveGroupCounts = new Map(REFERENCE_SET_ARCHIVE.groups.map((group) => [group.name, group.count]));
const STAGES = [
  { id: 'flat-color', output: 'flat-color', label: 'Flat colors', description: 'Emit one solid block per selected palette color.', sources: ['col'] },
  { id: 'gradient-masks', output: 'gradient-masks', label: 'Gradient masks', description: 'Axial, radial, square, border, and imported raster alpha fields.', sources: ['Gradient Layers Alpha Maps'] },
  { id: 'historic-gradients', output: 'gradient-variants', label: 'Gradient variant matrix', description: 'Export the embedded gradient variants.', sources: ['col bin 2'] },
  { id: 'downscale', output: 'downscaled', label: 'Downscale', description: 'Recolor and resize the embedded sources with Lanczos3.', sources: ['blue 64-8 24 bit'], color: '#0000ff' },
  { id: 'foreground-alpha', output: 'foreground-alpha', label: 'Foreground alpha', description: 'Recolor the 22 embedded foreground-alpha fields.', sources: ['red FG-Alpha'], color: '#ff0000' },
  { id: 'foreground-composites', output: 'foreground-composites', label: 'Foreground composites', description: 'Composite foreground-alpha fields over a palette of base colors.', sources: ['Red-col fg-alpha Print'], color: '#ff0000' },
  { id: 'elliptical-gradients', output: 'elliptical-gradients', label: 'Elliptical multi-stop gradients', description: 'Asymmetric highlights, shadows, eyes, and layered organic gradients.', sources: ['brown bear'], color: '#993300' },
  { id: 'layer-compositions', output: 'layer-compositions', label: 'Layer compositions', description: 'Combine multiple masks, colors, crops, offsets, and raster fields.', sources: ['out4 - modding', 'out4 - Select Library', 'out4-special'] },
  { id: 'sans-glyphs', output: 'glyphs/sans-serif', label: 'Sans-serif glyphs', description: 'Recolor the embedded sans-serif glyph rasters.', sources: ['No AA 64px Black+White'], color: '#000000', background: '#ffffff' },
  { id: 'serif-glyphs', output: 'glyphs/serif', label: 'Serif glyphs', description: 'Recolor the embedded serif glyph rasters.', sources: ['New folder'], color: '#ff0000', background: '#0000ff' },
  { id: 'numbered-results', output: 'ordered-results', label: 'Ordered result aliases', description: 'Create numbered pixel aliases to embedded gradient variants, with raster exceptions.', sources: ['result'] },
  { id: 'basic-gradients', label: 'Custom gradient permutations', description: 'Append selected palette × mask × overlay permutations.', sources: [], enabled: false },
].map((stage) => ({ ...stage, count: stage.sources.reduce((total, source) => total + archiveGroupCounts.get(source), 0) }));
const OUTPUT_PREFIXES = [
  ['blue 64-8 24 bit/', 'downscaled/'],
  ['brown bear/', 'elliptical-gradients/'],
  ['col bin 2/', 'gradient-variants/'],
  ['col/', 'flat-color/'],
  ['Gradient Layers Alpha Maps/', 'gradient-masks/'],
  ['New folder/', 'glyphs/serif/'],
  ['No AA 64px Black+White/', 'glyphs/sans-serif/'],
  ['out4 - modding/', 'layer-compositions/variants/'],
  ['out4 - Select Library/', 'layer-compositions/curated/'],
  ['out4-special/', 'layer-compositions/special/'],
  ['red FG-Alpha/', 'foreground-alpha/'],
  ['Red-col fg-alpha Print/print output/', 'foreground-composites/'],
  ['result/', 'ordered-results/'],
];

function outputPath(sourcePath) {
  const mapping = OUTPUT_PREFIXES.find(([prefix]) => sourcePath.startsWith(prefix));
  return mapping ? `${mapping[1]}${sourcePath.slice(mapping[0].length)}` : sourcePath;
}

const app = document.querySelector('#app');
app.innerHTML = `
  <article class="shell">
    <header><h1>Bingen</h1><p>build a bin block collection from colors, image operations, and reusable recipes</p></header>
    <section class="section step">
      <h2><span>1</span> Choose base colors</h2>
      <div class="step-body"><p class="help">These colors feed flat-color and custom generation stages. Click a tile to include or exclude it, then add any extra hex colors.</p><div id="palette" class="palette-grid"></div><label class="custom-colours">additional colors <input id="custom-colours" placeholder="#123456, #abcdef"></label></div>
    </section>
    <section class="section step">
      <h2><span>2</span> Assemble generation stages <code id="collection-count"></code></h2>
        <div id="stage-list" class="stage-list">${STAGES.map((stage) => `<article class="stage-card"><label class="stage-toggle"><input type="checkbox" name="generation-stage" value="${stage.id}" ${stage.enabled === false ? '' : 'checked'}><strong>${stage.label}</strong>${stage.count ? `<code>${stage.count} reference outputs</code>` : ''}</label><p>${stage.description}</p>${stage.color ? `<label>base / foreground <input type="color" name="${stage.id}-color" data-stage-color="${stage.id}" value="${stage.color}"></label>` : ''}${stage.background ? `<label>background <input type="color" name="${stage.id}-background" data-stage-background="${stage.id}" value="${stage.background}"></label>` : ''}${stage.id === 'downscale' ? '<label>output size <input type="number" name="downscale-size" data-stage-size="downscale" min="1" max="64" value="8"></label>' : ''}${stage.id === 'basic-gradients' ? `<details><summary>choose masks</summary><div id="preset-list" class="generation-grid">${Object.entries(PRESETS).map(([name, item]) => `<label><input type="checkbox" name="collection-preset" value="${name}" checked> ${item.label}</label>`).join('')}</div></details>` : ''}</article>`).join('')}</div>
      <details class="config-editor"><summary>collection recipe JSON</summary><p class="help">Edit the complete family and permutation configuration directly, or load the current form state before making changes.</p><textarea id="collection-config" spellcheck="false"></textarea><div class="actions"><button id="load-config" type="button">load current configuration</button><button id="apply-config" type="button">apply JSON configuration</button></div></details>
    </section>
    <section class="section step catalogue">
      <h2><span>3</span> Review and export</h2>
      <div class="collection-preview">
        <div><h3>Collection preview</h3><p class="help">This is the folder tree and texture atlas that will be written into the ZIP. The atlas is rendered locally and does not require a download.</p><pre id="folder-tree"></pre></div>
        <div><h3>Texture atlas <code id="atlas-dimensions"></code></h3><div class="atlas-panel"><canvas id="atlas-preview" aria-label="Generated collection texture atlas"></canvas></div></div>
      </div>
      <div class="actions"><button id="download-collection" type="button">build collection ZIP</button><output id="collection-status">not built</output></div>
    </section>
  </article>`;

const paletteGrid = document.querySelector('#palette');
const collectionCount = document.querySelector('#collection-count');
const collectionStatus = document.querySelector('#collection-status');
const atlasPreview = document.querySelector('#atlas-preview');
const folderTree = document.querySelector('#folder-tree');
const archiveFilesByPath = new Map(REFERENCE_SET_ARCHIVE.files.map((file) => [file.path, file]));
const archivePixelCache = new Map();
let archiveAtlasBitmap;

function canvasFromPixels(pixels, width, height) {
  const output = document.createElement('canvas');
  output.width = width;
  output.height = height;
  output.getContext('2d').putImageData(new ImageData(pixels, width, height), 0, 0);
  return output;
}

function download(name, blob) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

function canvasBlob(source) {
  return new Promise((resolve, reject) => source.toBlob((blob) => blob ? resolve(blob) : reject(new Error('PNG encoding failed.')), 'image/png'));
}

function base64Blob(base64, type = 'image/png') {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) bytes[index] = binary.charCodeAt(index);
  return new Blob([bytes], { type });
}

async function archivePixels(path) {
  if (archivePixelCache.has(path)) return archivePixelCache.get(path);
  const pending = decodeArchivePixels(path);
  archivePixelCache.set(path, pending);
  return pending;
}

async function decodeArchivePixels(path) {
  const file = archiveFilesByPath.get(path);
  if (!file) throw new Error(`Missing embedded archive image: ${path}`);
  const image = await createImageBitmap(base64Blob(file.base64));
  const source = document.createElement('canvas');
  source.width = image.width;
  source.height = image.height;
  const sourceContext = source.getContext('2d');
  sourceContext.drawImage(image, 0, 0);
  image.close();
  return { width: source.width, height: source.height, pixels: sourceContext.getImageData(0, 0, source.width, source.height).data };
}

function compileFamilyRecipe(recipe) {
  return compileRecipe(recipe, {
    operations: REFERENCE_SET_IMAGE_OPERATIONS,
    resolveRaster: (_assetId, asset) => archivePixels(asset.path),
  });
}

async function flatColorOutputs() {
  return (await compileFamilyRecipe(createFlatColorRecipe(collectionPlan().colours))).outputs;
}

async function downscaledOutputs() {
  return (await compileFamilyRecipe(createDownscaledRecipe(REFERENCE_SET_ARCHIVE, {
    color: stageColor('downscale'),
    size: Number(document.querySelector('[data-stage-size="downscale"]').value),
  }))).outputs;
}

async function gradientMaskOutputs() {
  return (await compileFamilyRecipe(createGradientMaskRecipe(REFERENCE_SET_ARCHIVE))).outputs;
}

async function foregroundAlphaOutputs() {
  return (await compileFamilyRecipe(createForegroundAlphaRecipe(REFERENCE_SET_ARCHIVE, {
    color: stageColor('foreground-alpha'),
  }))).outputs;
}

async function foregroundCompositeOutputs() {
  return (await compileFamilyRecipe(createForegroundCompositeRecipe(REFERENCE_SET_ARCHIVE, {
    color: stageColor('foreground-composites'),
  }))).outputs;
}

async function organicGradientOutputs() {
  return (await compileFamilyRecipe(createOrganicGradientRecipe(REFERENCE_SET_ARCHIVE, {
    color: stageColor('elliptical-gradients'),
  }))).outputs;
}

async function glyphOutputs(familyId) {
  const sans = familyId === 'sans-glyphs';
  return (await compileFamilyRecipe(createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
    familyId,
    sourceForeground: sans ? '#000000' : '#ff0000',
    sourceBackground: sans ? '#ffffff' : '#0000ff',
    foreground: stageColor(familyId),
    background: document.querySelector(`[data-stage-background="${familyId}"]`).value,
  }))).outputs;
}

function stageEnabled(id) {
  return document.querySelector(`[name="generation-stage"][value="${id}"]`)?.checked ?? false;
}

function stageColor(id) {
  return document.querySelector(`[data-stage-color="${id}"]`)?.value;
}

function customPalette() {
  const values = document.querySelector('#custom-colours').value.split(',').map((value) => value.trim()).filter(Boolean);
  return values.map((colour, index) => {
    if (!/^#[\da-f]{6}$/i.test(colour)) throw new Error(`Invalid custom color: ${colour}`);
    return [`custom_${String(index + 1).padStart(2, '0')}`, colour.toLowerCase()];
  });
}

function collectionPlan() {
  const colours = [...paletteGrid.querySelectorAll('.swatch.selected')].map((item) => [item.dataset.name, item.dataset.colour]);
  const presets = [...document.querySelectorAll('[name="collection-preset"]:checked')].map((item) => item.value);
  return { colours: [...colours, ...customPalette()], presets };
}

function selectedArchiveGroups() {
  const selectedStages = new Set([...document.querySelectorAll('[name="generation-stage"]:checked')].map((input) => input.value));
  const selectedSources = new Set(STAGES.filter((stage) => selectedStages.has(stage.id)).flatMap((stage) => stage.sources));
  return REFERENCE_SET_ARCHIVE.groups.filter((group) => selectedSources.has(group.name));
}

function selectedStages() {
  const selected = new Set([...document.querySelectorAll('[name="generation-stage"]:checked')].map((input) => input.value));
  return STAGES.filter((stage) => selected.has(stage.id));
}

function activeRecipeCount() {
  return STAGES.filter((stage) => stage.count && stageEnabled(stage.id)).length + (stageEnabled('basic-gradients') ? 1 : 0);
}

function customPlan() {
  return document.querySelector('[name="generation-stage"][value="basic-gradients"]').checked ? collectionPlan() : { colours: [], presets: [] };
}

function collectionConfiguration() {
  return {
    format: 'bin-block-configuration/v1',
    palette: [...paletteGrid.querySelectorAll('.swatch.selected')].map((item) => item.dataset.name),
    colours: document.querySelector('#custom-colours').value.split(',').map((value) => value.trim()).filter(Boolean),
    stages: [...document.querySelectorAll('[name="generation-stage"]')].map((input) => {
      const stage = STAGES.find((item) => item.id === input.value);
      return {
        id: stage.id,
        enabled: input.checked,
        color: document.querySelector(`[data-stage-color="${stage.id}"]`)?.value,
        background: document.querySelector(`[data-stage-background="${stage.id}"]`)?.value,
        size: Number(document.querySelector(`[data-stage-size="${stage.id}"]`)?.value) || undefined,
        presets: stage.id === 'basic-gradients' ? [...document.querySelectorAll('[name="collection-preset"]:checked')].map((item) => item.value) : undefined,
      };
    }),
    overlays,
  };
}

function loadConfigurationEditor() {
  document.querySelector('#collection-config').value = JSON.stringify(collectionConfiguration(), null, 2);
}

function applyCollectionConfiguration() {
  const configuration = JSON.parse(document.querySelector('#collection-config').value);
  if (configuration.format !== 'bin-block-configuration/v1') throw new Error('Unsupported collection configuration format.');
  const stageConfigurations = new Map((configuration.stages ?? []).map((stage) => [stage.id, stage]));
  document.querySelectorAll('[name="generation-stage"]').forEach((input) => {
    const stage = stageConfigurations.get(input.value);
    input.checked = Boolean(stage?.enabled);
    const color = document.querySelector(`[data-stage-color="${input.value}"]`);
    const background = document.querySelector(`[data-stage-background="${input.value}"]`);
    const size = document.querySelector(`[data-stage-size="${input.value}"]`);
    if (color && stage?.color) color.value = stage.color;
    if (background && stage?.background) background.value = stage.background;
    if (size && stage?.size) size.value = stage.size;
  });
  const paletteNames = new Set(configuration.palette ?? []);
  paletteGrid.querySelectorAll('.swatch').forEach((button) => {
    button.classList.toggle('selected', paletteNames.has(button.dataset.name));
    button.setAttribute('aria-pressed', button.classList.contains('selected'));
  });
  document.querySelector('#custom-colours').value = (configuration.colours ?? []).join(', ');
  const presets = new Set(stageConfigurations.get('basic-gradients')?.presets ?? []);
  document.querySelectorAll('[name="collection-preset"]').forEach((input) => { input.checked = presets.has(input.value); });
}

async function updateCollectionCount() {
  try {
    const groups = selectedArchiveGroups();
    const paletteEntries = collectionPlan().colours;
    const archiveCount = groups.reduce((total, group) => total + (group.name === 'col' ? paletteEntries.length : group.count), 0);
    const { colours, presets } = customPlan();
    const results = colours.length * presets.length * overlays.length;
    const customCount = colours.length + presets.length + results * 2;
    collectionCount.textContent = `${archiveCount + customCount} images`;
    await renderCollectionPreview(groups, colours, presets, results);
  } catch {
    collectionCount.textContent = 'fix custom colors';
    folderTree.textContent = 'Fix custom colors to preview the collection.';
  }
}

async function getArchiveAtlasBitmap() {
  archiveAtlasBitmap ||= await createImageBitmap(base64Blob(REFERENCE_SET_ARCHIVE.atlas.base64));
  return archiveAtlasBitmap;
}

async function renderCollectionPreview(groups, colours, presets, results) {
  if (!groups.length && !results) {
    atlasPreview.width = 1;
    atlasPreview.height = 1;
    folderTree.textContent = 'Select at least one base colour and generation type.';
    document.querySelector('#atlas-dimensions').textContent = '';
    return;
  }
  const width = 64;
  const columns = REFERENCE_SET_ARCHIVE.atlas.columns;
  const archiveRows = groups.reduce((total, group) => total + group.atlasRows + 1, 0);
  const customRows = results ? 5 + Math.ceil(results / columns) * 2 + 1 : 0;
  atlasPreview.width = columns * width;
  atlasPreview.height = Math.max(1, (archiveRows + customRows) * width);
  const atlasContext = atlasPreview.getContext('2d');
  let destinationRow = 0;
  if (groups.length) {
    const bitmap = await getArchiveAtlasBitmap();
    for (const group of groups) {
      const pipelineOutputs = group.name === 'col'
        ? await flatColorOutputs()
        : group.name === 'Gradient Layers Alpha Maps'
          ? await gradientMaskOutputs()
        : group.name === 'blue 64-8 24 bit'
          ? await downscaledOutputs()
          : group.name === 'red FG-Alpha'
            ? await foregroundAlphaOutputs()
          : group.name === 'Red-col fg-alpha Print'
              ? await foregroundCompositeOutputs()
              : group.name === 'brown bear'
                  ? await organicGradientOutputs()
                  : group.name === 'No AA 64px Black+White'
                      ? await glyphOutputs('sans-glyphs')
                      : group.name === 'New folder'
                        ? await glyphOutputs('serif-glyphs')
          : undefined;
      if (pipelineOutputs) {
        const files = pipelineOutputs ?? REFERENCE_SET_ARCHIVE.files.filter((file) => file.path.split('/')[0] === group.name);
        for (const [index, file] of files.entries()) {
          atlasContext.putImageData(new ImageData(file.image.pixels, file.image.width, file.image.height), (index % columns) * width, (destinationRow + Math.floor(index / columns)) * width);
        }
      } else {
        atlasContext.drawImage(bitmap, 0, group.atlasRow * width, atlasPreview.width, group.atlasRows * width, 0, destinationRow * width, atlasPreview.width, group.atlasRows * width);
      }
      destinationRow += group.atlasRows + 1;
    }
  }
  if (!results) {
    document.querySelector('#atlas-dimensions').textContent = `${atlasPreview.width}x${atlasPreview.height}`;
    folderTree.textContent = `bin-block-collection/\n${selectedStages().filter((stage) => stage.count).map((stage) => `├── ${stage.output}/  ${stage.id === 'flat-color' ? collectionPlan().colours.length : stage.count} images`).join('\n')}\n├── recipes/  ${activeRecipeCount()} executable recipes + schema\n├── texture-atlas.png\n└── manifest.json`;
    return;
  }
  const customOutputs = (await compileFamilyRecipe(createCustomPermutationRecipe(colours, presets, overlays))).outputs;
  const flatOutputs = customOutputs.filter((output) => output.stage === 'custom-flat');
  const maskOutputs = customOutputs.filter((output) => output.stage === 'custom-masks');
  const variantOutputs = customOutputs.filter((output) => output.stage === 'custom-variants');
  const resultOutputs = customOutputs.filter((output) => output.stage === 'custom-results');
  flatOutputs.forEach((output, index) => {
    atlasContext.putImageData(new ImageData(output.image.pixels, output.image.width, output.image.height), index * width, destinationRow * width);
  });
  maskOutputs.forEach((output, index) => {
    atlasContext.putImageData(new ImageData(output.image.pixels, output.image.width, output.image.height), index * width, (destinationRow + 2) * width);
  });
  const variantsRow = destinationRow + 4;
  const resultsRow = variantsRow + Math.ceil(results / columns) + 1;
  variantOutputs.forEach((output, index) => {
    const x = (index % columns) * width;
    const rowOffset = Math.floor(index / columns) * width;
    atlasContext.putImageData(new ImageData(output.image.pixels, output.image.width, output.image.height), x, variantsRow * width + rowOffset);
  });
  resultOutputs.forEach((output, index) => {
    const x = (index % columns) * width;
    const rowOffset = Math.floor(index / columns) * width;
    atlasContext.putImageData(new ImageData(output.image.pixels, output.image.width, output.image.height), x, resultsRow * width + rowOffset);
  });
  document.querySelector('#atlas-dimensions').textContent = `${atlasPreview.width}x${atlasPreview.height}`;
  folderTree.textContent = `bin-block-collection/
${selectedStages().filter((stage) => stage.count).map((stage) => `├── ${stage.output}/  ${stage.id === 'flat-color' ? collectionPlan().colours.length : stage.count} generated images`).join('\n')}
├── generated/flat/              ${colours.length} custom base blocks
├── generated/masks/             ${presets.length} custom masks
├── generated/variants/          ${results} custom variants
├── generated/results/           ${results} custom result aliases
├── recipes/                     ${activeRecipeCount()} executable recipes + schema
├── texture-atlas.png            ${atlasPreview.width}x${atlasPreview.height}, all PNG outputs
└── manifest.json                every output recipe`;
}

async function addCustomOutputs(zip, root, manifest, recipeDocuments) {
  const { colours, presets } = customPlan();
  if (!colours.length || !presets.length) return 0;
  const recipe = createCustomPermutationRecipe(colours, presets, overlays);
  const outputs = (await compileFamilyRecipe(recipe)).outputs;
  for (const [index, output] of outputs.entries()) {
    zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
    manifest.customRecipes.push({ path: output.path, stage: output.stage, key: output.key, provenance: output.provenance });
    if (index % 25 === 0) collectionStatus.textContent = `writing ${index} of ${outputs.length} custom recipe outputs...`;
    }
  recipeDocuments.push({ stage: 'custom-permutations', path: 'recipes/custom-permutations.json', recipe });
  return outputs.length;
}

async function buildCollection() {
  const root = 'bin-block-collection';
  const zip = new JSZip();
  const recipeDocuments = [];
  const groups = selectedArchiveGroups();
  const selectedNames = new Set(groups.map((group) => group.name));
  const paletteEntries = collectionPlan().colours;
  const customFlatCount = stageEnabled('flat-color') ? paletteEntries.filter(([name]) => name.startsWith('custom_')).length : 0;
  const selectedPaletteNames = new Set(paletteEntries.map(([name]) => `${name}.png`));
  const selectedFiles = REFERENCE_SET_ARCHIVE.files.filter((file) => {
    const group = file.path.split('/')[0];
    if (!selectedNames.has(group)) return false;
    return group !== 'col' || selectedPaletteNames.has(file.path.split('/').at(-1));
  });
  const materializedPaths = [];
  const emitRecipe = async (stage, path, recipe, { materialize = true } = {}) => {
    if (materialize) {
      for (const output of (await compileFamilyRecipe(recipe)).outputs) {
        zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
        materializedPaths.push(output.path);
      }
    } else {
      parseRecipeDocument(recipe);
    }
    recipeDocuments.push({ stage, path, recipe });
  };
  if (!selectedFiles.length && !customPlan().colours.length && !(stageEnabled('flat-color') && paletteEntries.length)) throw new Error('Enable at least one generation stage.');
  collectionStatus.textContent = `packing ${selectedFiles.length} embedded images...`;
  for (const [index, file] of selectedFiles.entries()) {
    zip.file(`${root}/${outputPath(file.path)}`, file.base64, { base64: true });
    if (index % 100 === 0) collectionStatus.textContent = `packing ${index} of ${selectedFiles.length} embedded images...`;
  }
  if (stageEnabled('flat-color')) {
    collectionStatus.textContent = 'compiling flat-color recipe...';
    const recipe = createFlatColorRecipe(paletteEntries);
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
      materializedPaths.push(output.path);
    }
    recipeDocuments.push({ stage: 'flat-color', path: 'recipes/flat-color.json', recipe });
  }
  if (stageEnabled('gradient-masks')) {
    collectionStatus.textContent = 'compiling gradient-mask recipe...';
    const recipe = createGradientMaskRecipe(REFERENCE_SET_ARCHIVE);
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
      materializedPaths.push(output.path);
    }
    recipeDocuments.push({ stage: 'gradient-masks', path: 'recipes/gradient-masks.json', recipe });
  }
  if (stageEnabled('downscale')) {
    collectionStatus.textContent = 'compiling downscaled recipe...';
    const recipe = createDownscaledRecipe(REFERENCE_SET_ARCHIVE, {
      color: stageColor('downscale'),
      size: Number(document.querySelector('[data-stage-size="downscale"]').value),
    });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
      materializedPaths.push(output.path);
    }
    recipeDocuments.push({ stage: 'downscaled', path: 'recipes/downscaled.json', recipe });
  }
  if (stageEnabled('foreground-alpha')) {
    collectionStatus.textContent = 'compiling foreground-alpha recipe...';
    const recipe = createForegroundAlphaRecipe(REFERENCE_SET_ARCHIVE, { color: stageColor('foreground-alpha') });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
      materializedPaths.push(output.path);
    }
    recipeDocuments.push({ stage: 'foreground-alpha', path: 'recipes/foreground-alpha.json', recipe });
  }
  if (stageEnabled('foreground-composites')) {
    collectionStatus.textContent = 'compiling foreground-composite recipe...';
    const recipe = createForegroundCompositeRecipe(REFERENCE_SET_ARCHIVE, { color: stageColor('foreground-composites') });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      zip.file(`${root}/${output.path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
      materializedPaths.push(output.path);
    }
    recipeDocuments.push({ stage: 'foreground-composites', path: 'recipes/foreground-composites.json', recipe });
  }
  if (stageEnabled('historic-gradients')) {
    collectionStatus.textContent = 'compiling gradient-variant recipe...';
    await emitRecipe('gradient-variants', 'recipes/gradient-variants.json', createRasterFamilyRecipe(REFERENCE_SET_ARCHIVE, 'gradient-variants'), { materialize: false });
  }
  if (stageEnabled('elliptical-gradients')) {
    collectionStatus.textContent = 'compiling elliptical-gradient recipe...';
    await emitRecipe('elliptical-gradients', 'recipes/elliptical-gradients.json', createOrganicGradientRecipe(REFERENCE_SET_ARCHIVE, {
      color: stageColor('elliptical-gradients'),
    }));
  }
  if (stageEnabled('layer-compositions')) {
    collectionStatus.textContent = 'compiling layer-composition recipe...';
    await emitRecipe('layer-compositions', 'recipes/layer-compositions.json', createRasterFamilyRecipe(REFERENCE_SET_ARCHIVE, 'layer-compositions'), { materialize: false });
  }
  if (stageEnabled('sans-glyphs')) {
    collectionStatus.textContent = 'compiling sans-glyph recipe...';
    await emitRecipe('sans-glyphs', 'recipes/sans-glyphs.json', createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
      familyId: 'sans-glyphs',
      sourceForeground: '#000000',
      sourceBackground: '#ffffff',
      foreground: stageColor('sans-glyphs'),
      background: document.querySelector('[data-stage-background="sans-glyphs"]').value,
    }));
  }
  if (stageEnabled('serif-glyphs')) {
    collectionStatus.textContent = 'compiling serif-glyph recipe...';
    await emitRecipe('serif-glyphs', 'recipes/serif-glyphs.json', createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
      familyId: 'serif-glyphs',
      sourceForeground: '#ff0000',
      sourceBackground: '#0000ff',
      foreground: stageColor('serif-glyphs'),
      background: document.querySelector('[data-stage-background="serif-glyphs"]').value,
    }));
  }
  if (stageEnabled('numbered-results')) {
    collectionStatus.textContent = 'compiling ordered-result recipe...';
    await emitRecipe('ordered-results', 'recipes/ordered-results.json', createOrderedResultsRecipe(REFERENCE_SET_ARCHIVE), { materialize: false });
  }
  const manifest = {
    format: 'bin-block-collection/v3',
    recipeFormat: 'bin-block-recipe/v1',
    configuration: collectionConfiguration(),
    selectedStages: selectedStages().map((stage) => stage.id),
    customRecipes: [],
  };
  const customCount = await addCustomOutputs(zip, root, manifest, recipeDocuments);
  const imageCount = selectedFiles.length + customFlatCount + customCount;
  const materialized = new Set(materializedPaths);
  manifest.imageCount = imageCount;
  manifest.recipes = recipeDocuments.map(({ stage, path, recipe }) => ({
    stage,
    path,
    format: recipe.format,
    profile: recipe.profile,
    images: recipe.metadata.imageCount,
  }));
  manifest.provenance = {
    materializedPaths,
    sourceBytePaths: selectedFiles.map((file) => outputPath(file.path)).filter((path) => !materialized.has(path)),
    pixelAliases: stageEnabled('numbered-results') ? 960 : 0,
    rasterAliasExceptions: stageEnabled('numbered-results') ? 12 : 0,
  };
  for (const { path, recipe } of recipeDocuments) zip.file(`${root}/${path}`, JSON.stringify(recipe, null, 2));
  if (recipeDocuments.length) zip.file(`${root}/recipes/bin-block-recipe-v1.schema.json`, JSON.stringify(recipeJsonSchema(), null, 2));
  await updateCollectionCount();
  zip.file(`${root}/texture-atlas.png`, await canvasBlob(atlasPreview));
  manifest.atlas = { width: atlasPreview.width, height: atlasPreview.height, columns: REFERENCE_SET_ARCHIVE.atlas.columns };
  zip.file(`${root}/manifest.json`, JSON.stringify(manifest, null, 2));
  collectionStatus.textContent = 'compressing collection...';
  const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 6 } }, ({ percent }) => { collectionStatus.textContent = `compressing ${Math.round(percent)}%`; });
  download('bin-block-collection.zip', blob);
  collectionStatus.textContent = `${imageCount} images downloaded`;
}

document.querySelector('#download-collection').addEventListener('click', async () => {
  try { await buildCollection(); } catch (error) { collectionStatus.textContent = error.message; }
});

paletteGrid.innerHTML = DEFAULT_PALETTE.map(([name, colour]) => `<button class="swatch selected" type="button" style="--colour: ${colour}" data-name="${name}" data-colour="${colour}" aria-pressed="true"><span></span><code>${colour}</code></button>`).join('');
paletteGrid.addEventListener('click', (event) => {
  const button = event.target.closest('[data-colour]');
  if (!button) return;
  button.classList.toggle('selected');
  button.setAttribute('aria-pressed', button.classList.contains('selected'));
  updateCollectionCount();
});
document.querySelector('#preset-list').addEventListener('change', updateCollectionCount);
document.querySelector('#custom-colours').addEventListener('input', updateCollectionCount);
document.querySelector('#stage-list').addEventListener('change', updateCollectionCount);
document.querySelector('#load-config').addEventListener('click', loadConfigurationEditor);
document.querySelector('#apply-config').addEventListener('click', async () => {
  try {
    applyCollectionConfiguration();
    await updateCollectionCount();
    collectionStatus.textContent = 'configuration applied';
  } catch (error) {
    collectionStatus.textContent = error.message;
  }
});

updateCollectionCount();
loadConfigurationEditor();
