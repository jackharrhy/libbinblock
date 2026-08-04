import JSZip from 'jszip';
import './style.css';
import { createRecipeNotebook } from './binscript-notebook.js';
import { DEFAULT_PALETTE, PRESETS } from './core.js';
import type { PaletteEntry, PresetName } from './core.js';
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
import type { PermutationOverlay, ReferenceSetRecipeInput } from './reference-set-recipe.js';
import { compileRecipe } from './recipe-executor.js';
import type { ArtifactProvenance, ImageData as CompiledImage, ImageDataInput, RecipeArtifact } from './recipe-executor.js';
import { parseRecipeDocument, recipeJsonSchema } from './recipe-schema.js';
import { REFERENCE_SET_ARCHIVE } from 'virtual:reference-set-archive';
import type { ReferenceSetArchiveFile, ReferenceSetArchiveGroup } from 'virtual:reference-set-archive';

type ElementConstructor<T extends Element> = new () => T;

interface StageDefinition {
  id: string;
  output?: string;
  label: string;
  description: string;
  sources: readonly string[];
  color?: string;
  background?: string;
  enabled?: boolean;
  count: number;
}

interface StageConfiguration {
  id: string;
  enabled: boolean;
  color?: string;
  background?: string;
  size?: number;
  presets?: PresetName[];
}

interface CollectionConfiguration {
  format: 'bin-block-configuration/v1';
  palette: string[];
  colours: string[];
  stages: StageConfiguration[];
  overlays: readonly PermutationOverlay[];
}

interface CollectionPlan {
  colours: PaletteEntry[];
  presets: PresetName[];
}

interface RecipeDocumentEntry {
  stage: string;
  path: string;
  recipe: ReferenceSetRecipeInput;
}

interface CustomRecipeManifestEntry {
  path: string;
  stage: string;
  key: string;
  provenance: ArtifactProvenance;
}

interface RecipeManifestEntry {
  stage: string;
  path: string;
  format: string;
  profile?: string;
  images: number;
}

interface CollectionProvenance {
  materializedPaths: string[];
  sourceBytePaths: string[];
  pixelAliases: number;
  rasterAliasExceptions: number;
}

interface CollectionManifest {
  format: 'bin-block-collection/v3';
  recipeFormat: 'bin-block-recipe/v1';
  configuration: CollectionConfiguration;
  selectedStages: string[];
  customRecipes: CustomRecipeManifestEntry[];
  imageCount?: number;
  recipes?: RecipeManifestEntry[];
  provenance?: CollectionProvenance;
  atlas?: { width: number; height: number; columns: number };
}

function requiredElement<T extends Element>(selector: string, constructor: ElementConstructor<T>, root: ParentNode = document): T {
  const element = root.querySelector(selector);
  if (!(element instanceof constructor)) throw new Error(`Required element not found or has the wrong type: ${selector}`);
  return element;
}

function optionalElement<T extends Element>(
  selector: string,
  constructor: ElementConstructor<T>,
  root: ParentNode = document,
): T | undefined {
  const element = root.querySelector(selector);
  if (element === null) return undefined;
  if (!(element instanceof constructor)) throw new Error(`Element has the wrong type: ${selector}`);
  return element;
}

function requiredCanvasContext(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
  const context = canvas.getContext('2d');
  if (!context) throw new Error('2D canvas rendering is unavailable.');
  return context;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function artifactPath(artifact: RecipeArtifact): string {
  if (!artifact.path) throw new Error(`Compiled output ${artifact.stage}/${artifact.key} has no path.`);
  return artifact.path;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function stringArray(value: unknown): string[] {
  return Array.isArray(value) && value.every((item) => typeof item === 'string') ? value : [];
}

function isPresetName(value: string): value is PresetName {
  return value in PRESETS;
}

function parseStageConfiguration(value: unknown): StageConfiguration | undefined {
  if (!isRecord(value) || typeof value.id !== 'string') return undefined;
  const presets = stringArray(value.presets).filter(isPresetName);
  return {
    id: value.id,
    enabled: Boolean(value.enabled),
    ...(typeof value.color === 'string' ? { color: value.color } : {}),
    ...(typeof value.background === 'string' ? { background: value.background } : {}),
    ...(typeof value.size === 'number' ? { size: value.size } : {}),
    ...(presets.length ? { presets } : {}),
  };
}

function parseCollectionConfiguration(value: unknown): CollectionConfiguration {
  if (!isRecord(value) || value.format !== 'bin-block-configuration/v1') {
    throw new Error('Unsupported collection configuration format.');
  }
  return {
    format: value.format,
    palette: stringArray(value.palette),
    colours: stringArray(value.colours),
    stages: Array.isArray(value.stages)
      ? value.stages.map(parseStageConfiguration).filter((stage): stage is StageConfiguration => stage !== undefined)
      : [],
    overlays,
  };
}

const overlays: readonly PermutationOverlay[] = [
  { name: 'blk100', colour: '#000000' },
  { name: 'wht100', colour: '#ffffff' },
];
const archiveGroupCounts = new Map(REFERENCE_SET_ARCHIVE.groups.map((group) => [group.name, group.count]));
const STAGES: readonly StageDefinition[] = [
  {
    id: 'flat-color',
    output: 'flat-color',
    label: 'Flat colors',
    description: 'Emit one solid block per selected palette color.',
    sources: ['col'],
  },
  {
    id: 'gradient-masks',
    output: 'gradient-masks',
    label: 'Gradient masks',
    description: 'Axial, radial, square, border, and imported raster alpha fields.',
    sources: ['Gradient Layers Alpha Maps'],
  },
  {
    id: 'historic-gradients',
    output: 'gradient-variants',
    label: 'Gradient variant matrix',
    description: 'Export the embedded gradient variants.',
    sources: ['col bin 2'],
  },
  {
    id: 'downscale',
    output: 'downscaled',
    label: 'Downscale',
    description: 'Recolor and resize the embedded sources with Lanczos3.',
    sources: ['blue 64-8 24 bit'],
    color: '#0000ff',
  },
  {
    id: 'foreground-alpha',
    output: 'foreground-alpha',
    label: 'Foreground alpha',
    description: 'Recolor the 22 embedded foreground-alpha fields.',
    sources: ['red FG-Alpha'],
    color: '#ff0000',
  },
  {
    id: 'foreground-composites',
    output: 'foreground-composites',
    label: 'Foreground composites',
    description: 'Composite foreground-alpha fields over a palette of base colors.',
    sources: ['Red-col fg-alpha Print'],
    color: '#ff0000',
  },
  {
    id: 'elliptical-gradients',
    output: 'elliptical-gradients',
    label: 'Elliptical multi-stop gradients',
    description: 'Asymmetric highlights, shadows, eyes, and layered organic gradients.',
    sources: ['brown bear'],
    color: '#993300',
  },
  {
    id: 'layer-compositions',
    output: 'layer-compositions',
    label: 'Layer compositions',
    description: 'Combine multiple masks, colors, crops, offsets, and raster fields.',
    sources: ['out4 - modding', 'out4 - Select Library', 'out4-special'],
  },
  {
    id: 'sans-glyphs',
    output: 'glyphs/sans-serif',
    label: 'Sans-serif glyphs',
    description: 'Recolor the embedded sans-serif glyph rasters.',
    sources: ['No AA 64px Black+White'],
    color: '#000000',
    background: '#ffffff',
  },
  {
    id: 'serif-glyphs',
    output: 'glyphs/serif',
    label: 'Serif glyphs',
    description: 'Recolor the embedded serif glyph rasters.',
    sources: ['New folder'],
    color: '#ff0000',
    background: '#0000ff',
  },
  {
    id: 'numbered-results',
    output: 'ordered-results',
    label: 'Ordered result aliases',
    description: 'Create numbered pixel aliases to embedded gradient variants, with raster exceptions.',
    sources: ['result'],
  },
  {
    id: 'basic-gradients',
    label: 'Custom gradient permutations',
    description: 'Append selected palette × mask × overlay permutations.',
    sources: [],
    enabled: false,
  },
].map((stage) => ({ ...stage, count: stage.sources.reduce((total, source) => total + (archiveGroupCounts.get(source) ?? 0), 0) }));
const OUTPUT_PREFIXES: readonly (readonly [source: string, output: string])[] = [
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

function outputPath(sourcePath: string): string {
  const mapping = OUTPUT_PREFIXES.find(([prefix]) => sourcePath.startsWith(prefix));
  return mapping ? `${mapping[1]}${sourcePath.slice(mapping[0].length)}` : sourcePath;
}

const app = requiredElement('#app', HTMLElement);
app.innerHTML = `
  <article class="shell">
    <header><h1>Bingen</h1><p>build a bin block collection from colors, image operations, and reusable recipes</p></header>
    <section class="section notebook-section">
      <h2><span>0</span> BinScript notebook <code>live recipe source</code></h2>
      <div class="notebook-intro">
        <div><strong>Code is the interface.</strong><p>Compose images with bindings, fluent operations, and CSS-inspired gradients. BinScript lowers into the existing typed recipe engine.</p></div>
        <div class="notebook-actions"><button id="run-notebook" type="button">run <kbd>⌘↵</kbd></button><button id="reset-notebook" type="button">reset example</button><output id="notebook-status">starting...</output></div>
      </div>
      <div id="binscript-editor" aria-label="BinScript recipe editor"></div>
    </section>
    <section class="section step">
      <h2><span>1</span> Choose base colors</h2>
      <div class="step-body"><p class="help">These colors feed flat-color and custom generation stages. Click a tile to include or exclude it, then add any extra hex colors.</p><div id="palette" class="palette-grid"></div><label class="custom-colours">additional colors <input id="custom-colours" placeholder="#123456, #abcdef"></label></div>
    </section>
    <section class="section step">
      <h2><span>2</span> Assemble generation stages <code id="collection-count"></code></h2>
        <div id="stage-list" class="stage-list">${STAGES.map(
          (stage) =>
            `<article class="stage-card"><label class="stage-toggle"><input type="checkbox" name="generation-stage" value="${stage.id}" ${stage.enabled === false ? '' : 'checked'}><strong>${stage.label}</strong>${stage.count ? `<code>${stage.count} reference outputs</code>` : ''}</label><p>${stage.description}</p>${stage.color ? `<label>base / foreground <input type="color" name="${stage.id}-color" data-stage-color="${stage.id}" value="${stage.color}"></label>` : ''}${stage.background ? `<label>background <input type="color" name="${stage.id}-background" data-stage-background="${stage.id}" value="${stage.background}"></label>` : ''}${stage.id === 'downscale' ? '<label>output size <input type="number" name="downscale-size" data-stage-size="downscale" min="1" max="64" value="8"></label>' : ''}${
              stage.id === 'basic-gradients'
                ? `<details><summary>choose masks</summary><div id="preset-list" class="generation-grid">${Object.entries(PRESETS)
                    .map(
                      ([name, item]) =>
                        `<label><input type="checkbox" name="collection-preset" value="${name}" checked> ${item.label}</label>`,
                    )
                    .join('')}</div></details>`
                : ''
            }</article>`,
        ).join('')}</div>
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

const paletteGrid = requiredElement('#palette', HTMLElement);
createRecipeNotebook({
  parent: requiredElement('#binscript-editor', HTMLElement),
  status: requiredElement('#notebook-status', HTMLOutputElement),
  runButton: requiredElement('#run-notebook', HTMLButtonElement),
  resetButton: requiredElement('#reset-notebook', HTMLButtonElement),
});
const collectionCount = requiredElement('#collection-count', HTMLElement);
const collectionStatus = requiredElement('#collection-status', HTMLOutputElement);
const atlasPreview = requiredElement('#atlas-preview', HTMLCanvasElement);
const folderTree = requiredElement('#folder-tree', HTMLPreElement);
const atlasDimensions = requiredElement('#atlas-dimensions', HTMLElement);
const archiveFilesByPath = new Map<string, ReferenceSetArchiveFile>(REFERENCE_SET_ARCHIVE.files.map((file) => [file.path, file]));
const archivePixelCache = new Map<string, Promise<ImageDataInput>>();
let archiveAtlasBitmap: ImageBitmap | undefined;

function imageData(image: CompiledImage): ImageData {
  return new ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height);
}

function canvasFromPixels(pixels: Uint8Array | Uint8ClampedArray, width: number, height: number): HTMLCanvasElement {
  const output = document.createElement('canvas');
  output.width = width;
  output.height = height;
  requiredCanvasContext(output).putImageData(new ImageData(new Uint8ClampedArray(pixels), width, height), 0, 0);
  return output;
}

function download(name: string, blob: Blob): void {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

function canvasBlob(source: HTMLCanvasElement): Promise<Blob> {
  return new Promise<Blob>((resolve, reject) =>
    source.toBlob((blob) => (blob ? resolve(blob) : reject(new Error('PNG encoding failed.'))), 'image/png'),
  );
}

function base64Blob(base64: string, type = 'image/png'): Blob {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) bytes[index] = binary.charCodeAt(index);
  return new Blob([bytes], { type });
}

async function archivePixels(path: string): Promise<ImageDataInput> {
  const cached = archivePixelCache.get(path);
  if (cached) return cached;
  const pending = decodeArchivePixels(path);
  archivePixelCache.set(path, pending);
  return pending;
}

async function decodeArchivePixels(path: string): Promise<ImageDataInput> {
  const file = archiveFilesByPath.get(path);
  if (!file) throw new Error(`Missing embedded archive image: ${path}`);
  const image = await createImageBitmap(base64Blob(file.base64));
  const source = document.createElement('canvas');
  source.width = image.width;
  source.height = image.height;
  const sourceContext = requiredCanvasContext(source);
  sourceContext.drawImage(image, 0, 0);
  image.close();
  return { width: source.width, height: source.height, pixels: sourceContext.getImageData(0, 0, source.width, source.height).data };
}

function compileFamilyRecipe(recipe: ReferenceSetRecipeInput) {
  return compileRecipe(recipe, {
    operations: REFERENCE_SET_IMAGE_OPERATIONS,
    resolveRaster: (_assetId, asset) => archivePixels(asset.path),
  });
}

async function flatColorOutputs(): Promise<RecipeArtifact[]> {
  return (await compileFamilyRecipe(createFlatColorRecipe(collectionPlan().colours))).outputs;
}

async function downscaledOutputs(): Promise<RecipeArtifact[]> {
  return (
    await compileFamilyRecipe(
      createDownscaledRecipe(REFERENCE_SET_ARCHIVE, {
        color: stageColor('downscale'),
        size: Number(requiredElement('[data-stage-size="downscale"]', HTMLInputElement).value),
      }),
    )
  ).outputs;
}

async function gradientMaskOutputs(): Promise<RecipeArtifact[]> {
  return (await compileFamilyRecipe(createGradientMaskRecipe(REFERENCE_SET_ARCHIVE))).outputs;
}

async function foregroundAlphaOutputs(): Promise<RecipeArtifact[]> {
  return (
    await compileFamilyRecipe(
      createForegroundAlphaRecipe(REFERENCE_SET_ARCHIVE, {
        color: stageColor('foreground-alpha'),
      }),
    )
  ).outputs;
}

async function foregroundCompositeOutputs(): Promise<RecipeArtifact[]> {
  return (
    await compileFamilyRecipe(
      createForegroundCompositeRecipe(REFERENCE_SET_ARCHIVE, {
        color: stageColor('foreground-composites'),
      }),
    )
  ).outputs;
}

async function organicGradientOutputs(): Promise<RecipeArtifact[]> {
  return (
    await compileFamilyRecipe(
      createOrganicGradientRecipe(REFERENCE_SET_ARCHIVE, {
        color: stageColor('elliptical-gradients'),
      }),
    )
  ).outputs;
}

async function glyphOutputs(familyId: 'sans-glyphs' | 'serif-glyphs'): Promise<RecipeArtifact[]> {
  const sans = familyId === 'sans-glyphs';
  return (
    await compileFamilyRecipe(
      createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
        familyId,
        sourceForeground: sans ? '#000000' : '#ff0000',
        sourceBackground: sans ? '#ffffff' : '#0000ff',
        foreground: stageColor(familyId),
        background: requiredElement(`[data-stage-background="${familyId}"]`, HTMLInputElement).value,
      }),
    )
  ).outputs;
}

function stageEnabled(id: string): boolean {
  return requiredElement(`[name="generation-stage"][value="${id}"]`, HTMLInputElement).checked;
}

function stageColor(id: string): string {
  return requiredElement(`[data-stage-color="${id}"]`, HTMLInputElement).value;
}

function customPalette(): PaletteEntry[] {
  const values = requiredElement('#custom-colours', HTMLInputElement)
    .value.split(',')
    .map((value) => value.trim())
    .filter(Boolean);
  return values.map((colour, index) => {
    if (!/^#[\da-f]{6}$/i.test(colour)) throw new Error(`Invalid custom color: ${colour}`);
    return [`custom_${String(index + 1).padStart(2, '0')}`, colour.toLowerCase()];
  });
}

function collectionPlan(): CollectionPlan {
  const colours: PaletteEntry[] = [...paletteGrid.querySelectorAll<HTMLButtonElement>('.swatch.selected')].map((item) => [
    item.dataset.name ?? '',
    item.dataset.colour ?? '',
  ]);
  const presets = [...document.querySelectorAll<HTMLInputElement>('[name="collection-preset"]:checked')]
    .map((item) => item.value)
    .filter(isPresetName);
  return { colours: [...colours, ...customPalette()], presets };
}

function selectedArchiveGroups(): ReferenceSetArchiveGroup[] {
  const selectedStages = new Set(
    [...document.querySelectorAll<HTMLInputElement>('[name="generation-stage"]:checked')].map((input) => input.value),
  );
  const selectedSources = new Set(STAGES.filter((stage) => selectedStages.has(stage.id)).flatMap((stage) => stage.sources));
  return REFERENCE_SET_ARCHIVE.groups.filter((group) => selectedSources.has(group.name));
}

function selectedStages(): StageDefinition[] {
  const selected = new Set(
    [...document.querySelectorAll<HTMLInputElement>('[name="generation-stage"]:checked')].map((input) => input.value),
  );
  return STAGES.filter((stage) => selected.has(stage.id));
}

function activeRecipeCount(): number {
  return STAGES.filter((stage) => stage.count && stageEnabled(stage.id)).length + (stageEnabled('basic-gradients') ? 1 : 0);
}

function customPlan(): CollectionPlan {
  return stageEnabled('basic-gradients') ? collectionPlan() : { colours: [], presets: [] };
}

function collectionConfiguration(): CollectionConfiguration {
  return {
    format: 'bin-block-configuration/v1',
    palette: [...paletteGrid.querySelectorAll<HTMLButtonElement>('.swatch.selected')].flatMap((item) =>
      item.dataset.name ? [item.dataset.name] : [],
    ),
    colours: requiredElement('#custom-colours', HTMLInputElement)
      .value.split(',')
      .map((value) => value.trim())
      .filter(Boolean),
    stages: [...document.querySelectorAll<HTMLInputElement>('[name="generation-stage"]')].map((input) => {
      const stage = STAGES.find((item) => item.id === input.value);
      if (!stage) throw new Error(`Unknown generation stage: ${input.value}`);
      return {
        id: stage.id,
        enabled: input.checked,
        color: optionalElement(`[data-stage-color="${stage.id}"]`, HTMLInputElement)?.value,
        background: optionalElement(`[data-stage-background="${stage.id}"]`, HTMLInputElement)?.value,
        size: Number(optionalElement(`[data-stage-size="${stage.id}"]`, HTMLInputElement)?.value) || undefined,
        presets:
          stage.id === 'basic-gradients'
            ? [...document.querySelectorAll<HTMLInputElement>('[name="collection-preset"]:checked')]
                .map((item) => item.value)
                .filter(isPresetName)
            : undefined,
      };
    }),
    overlays,
  };
}

function loadConfigurationEditor(): void {
  requiredElement('#collection-config', HTMLTextAreaElement).value = JSON.stringify(collectionConfiguration(), null, 2);
}

function applyCollectionConfiguration(): void {
  const parsed: unknown = JSON.parse(requiredElement('#collection-config', HTMLTextAreaElement).value);
  const configuration = parseCollectionConfiguration(parsed);
  const stageConfigurations = new Map((configuration.stages ?? []).map((stage) => [stage.id, stage]));
  document.querySelectorAll<HTMLInputElement>('[name="generation-stage"]').forEach((input) => {
    const stage = stageConfigurations.get(input.value);
    input.checked = Boolean(stage?.enabled);
    const color = optionalElement(`[data-stage-color="${input.value}"]`, HTMLInputElement);
    const background = optionalElement(`[data-stage-background="${input.value}"]`, HTMLInputElement);
    const size = optionalElement(`[data-stage-size="${input.value}"]`, HTMLInputElement);
    if (color && stage?.color) color.value = stage.color;
    if (background && stage?.background) background.value = stage.background;
    if (size && stage?.size) size.value = String(stage.size);
  });
  const paletteNames = new Set(configuration.palette ?? []);
  paletteGrid.querySelectorAll<HTMLButtonElement>('.swatch').forEach((button) => {
    button.classList.toggle('selected', button.dataset.name !== undefined && paletteNames.has(button.dataset.name));
    button.setAttribute('aria-pressed', String(button.classList.contains('selected')));
  });
  requiredElement('#custom-colours', HTMLInputElement).value = configuration.colours.join(', ');
  const presets = new Set(stageConfigurations.get('basic-gradients')?.presets ?? []);
  document.querySelectorAll<HTMLInputElement>('[name="collection-preset"]').forEach((input) => {
    input.checked = isPresetName(input.value) && presets.has(input.value);
  });
}

async function updateCollectionCount(): Promise<void> {
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

async function getArchiveAtlasBitmap(): Promise<ImageBitmap> {
  archiveAtlasBitmap ||= await createImageBitmap(base64Blob(REFERENCE_SET_ARCHIVE.atlas.base64));
  return archiveAtlasBitmap;
}

async function renderCollectionPreview(
  groups: readonly ReferenceSetArchiveGroup[],
  colours: readonly PaletteEntry[],
  presets: readonly PresetName[],
  results: number,
): Promise<void> {
  if (!groups.length && !results) {
    atlasPreview.width = 1;
    atlasPreview.height = 1;
    folderTree.textContent = 'Select at least one base colour and generation type.';
    atlasDimensions.textContent = '';
    return;
  }
  const width = 64;
  const columns = REFERENCE_SET_ARCHIVE.atlas.columns;
  const archiveRows = groups.reduce((total, group) => total + group.atlasRows + 1, 0);
  const customRows = results ? 5 + Math.ceil(results / columns) * 2 + 1 : 0;
  atlasPreview.width = columns * width;
  atlasPreview.height = Math.max(1, (archiveRows + customRows) * width);
  const atlasContext = requiredCanvasContext(atlasPreview);
  let destinationRow = 0;
  if (groups.length) {
    const bitmap = await getArchiveAtlasBitmap();
    for (const group of groups) {
      const pipelineOutputs =
        group.name === 'col'
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
          atlasContext.putImageData(
            imageData(file.image),
            (index % columns) * width,
            (destinationRow + Math.floor(index / columns)) * width,
          );
        }
      } else {
        atlasContext.drawImage(
          bitmap,
          0,
          group.atlasRow * width,
          atlasPreview.width,
          group.atlasRows * width,
          0,
          destinationRow * width,
          atlasPreview.width,
          group.atlasRows * width,
        );
      }
      destinationRow += group.atlasRows + 1;
    }
  }
  if (!results) {
    atlasDimensions.textContent = `${atlasPreview.width}x${atlasPreview.height}`;
    folderTree.textContent = `bin-block-collection/\n${selectedStages()
      .filter((stage) => stage.count)
      .map((stage) => `├── ${stage.output}/  ${stage.id === 'flat-color' ? collectionPlan().colours.length : stage.count} images`)
      .join('\n')}\n├── recipes/  ${activeRecipeCount()} executable recipes + schema\n├── texture-atlas.png\n└── manifest.json`;
    return;
  }
  const customOutputs = (await compileFamilyRecipe(createCustomPermutationRecipe(colours, presets, overlays))).outputs;
  const flatOutputs = customOutputs.filter((output) => output.stage === 'custom-flat');
  const maskOutputs = customOutputs.filter((output) => output.stage === 'custom-masks');
  const variantOutputs = customOutputs.filter((output) => output.stage === 'custom-variants');
  const resultOutputs = customOutputs.filter((output) => output.stage === 'custom-results');
  flatOutputs.forEach((output, index) => {
    atlasContext.putImageData(imageData(output.image), index * width, destinationRow * width);
  });
  maskOutputs.forEach((output, index) => {
    atlasContext.putImageData(imageData(output.image), index * width, (destinationRow + 2) * width);
  });
  const variantsRow = destinationRow + 4;
  const resultsRow = variantsRow + Math.ceil(results / columns) + 1;
  variantOutputs.forEach((output, index) => {
    const x = (index % columns) * width;
    const rowOffset = Math.floor(index / columns) * width;
    atlasContext.putImageData(imageData(output.image), x, variantsRow * width + rowOffset);
  });
  resultOutputs.forEach((output, index) => {
    const x = (index % columns) * width;
    const rowOffset = Math.floor(index / columns) * width;
    atlasContext.putImageData(imageData(output.image), x, resultsRow * width + rowOffset);
  });
  atlasDimensions.textContent = `${atlasPreview.width}x${atlasPreview.height}`;
  folderTree.textContent = `bin-block-collection/
${selectedStages()
  .filter((stage) => stage.count)
  .map((stage) => `├── ${stage.output}/  ${stage.id === 'flat-color' ? collectionPlan().colours.length : stage.count} generated images`)
  .join('\n')}
├── generated/flat/              ${colours.length} custom base blocks
├── generated/masks/             ${presets.length} custom masks
├── generated/variants/          ${results} custom variants
├── generated/results/           ${results} custom result aliases
├── recipes/                     ${activeRecipeCount()} executable recipes + schema
├── texture-atlas.png            ${atlasPreview.width}x${atlasPreview.height}, all PNG outputs
└── manifest.json                every output recipe`;
}

async function addCustomOutputs(
  zip: JSZip,
  root: string,
  manifest: CollectionManifest,
  recipeDocuments: RecipeDocumentEntry[],
): Promise<number> {
  const { colours, presets } = customPlan();
  if (!colours.length || !presets.length) return 0;
  const recipe = createCustomPermutationRecipe(colours, presets, overlays);
  const outputs = (await compileFamilyRecipe(recipe)).outputs;
  for (const [index, output] of outputs.entries()) {
    const path = artifactPath(output);
    zip.file(`${root}/${path}`, await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)));
    manifest.customRecipes.push({ path, stage: output.stage, key: output.key, provenance: output.provenance });
    if (index % 25 === 0) collectionStatus.textContent = `writing ${index} of ${outputs.length} custom recipe outputs...`;
  }
  recipeDocuments.push({ stage: 'custom-permutations', path: 'recipes/custom-permutations.json', recipe });
  return outputs.length;
}

async function buildCollection(): Promise<void> {
  const root = 'bin-block-collection';
  const zip = new JSZip();
  const recipeDocuments: RecipeDocumentEntry[] = [];
  const groups = selectedArchiveGroups();
  const selectedNames = new Set(groups.map((group) => group.name));
  const paletteEntries = collectionPlan().colours;
  const customFlatCount = stageEnabled('flat-color') ? paletteEntries.filter(([name]) => name.startsWith('custom_')).length : 0;
  const selectedPaletteNames = new Set(paletteEntries.map(([name]) => `${name}.png`));
  const selectedFiles = REFERENCE_SET_ARCHIVE.files.filter((file) => {
    const group = file.path.split('/')[0] ?? '';
    if (!selectedNames.has(group)) return false;
    return group !== 'col' || selectedPaletteNames.has(file.path.split('/').at(-1) ?? '');
  });
  const materializedPaths: string[] = [];
  const emitRecipe = async (
    stage: string,
    path: string,
    recipe: ReferenceSetRecipeInput,
    { materialize = true }: { materialize?: boolean } = {},
  ): Promise<void> => {
    if (materialize) {
      for (const output of (await compileFamilyRecipe(recipe)).outputs) {
        const outputFilePath = artifactPath(output);
        zip.file(
          `${root}/${outputFilePath}`,
          await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
        );
        materializedPaths.push(outputFilePath);
      }
    } else {
      parseRecipeDocument(recipe);
    }
    recipeDocuments.push({ stage, path, recipe });
  };
  if (!selectedFiles.length && !customPlan().colours.length && !(stageEnabled('flat-color') && paletteEntries.length))
    throw new Error('Enable at least one generation stage.');
  collectionStatus.textContent = `packing ${selectedFiles.length} embedded images...`;
  for (const [index, file] of selectedFiles.entries()) {
    zip.file(`${root}/${outputPath(file.path)}`, file.base64, { base64: true });
    if (index % 100 === 0) collectionStatus.textContent = `packing ${index} of ${selectedFiles.length} embedded images...`;
  }
  if (stageEnabled('flat-color')) {
    collectionStatus.textContent = 'compiling flat-color recipe...';
    const recipe = createFlatColorRecipe(paletteEntries);
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      const outputFilePath = artifactPath(output);
      zip.file(
        `${root}/${outputFilePath}`,
        await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
      );
      materializedPaths.push(outputFilePath);
    }
    recipeDocuments.push({ stage: 'flat-color', path: 'recipes/flat-color.json', recipe });
  }
  if (stageEnabled('gradient-masks')) {
    collectionStatus.textContent = 'compiling gradient-mask recipe...';
    const recipe = createGradientMaskRecipe(REFERENCE_SET_ARCHIVE);
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      const outputFilePath = artifactPath(output);
      zip.file(
        `${root}/${outputFilePath}`,
        await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
      );
      materializedPaths.push(outputFilePath);
    }
    recipeDocuments.push({ stage: 'gradient-masks', path: 'recipes/gradient-masks.json', recipe });
  }
  if (stageEnabled('downscale')) {
    collectionStatus.textContent = 'compiling downscaled recipe...';
    const recipe = createDownscaledRecipe(REFERENCE_SET_ARCHIVE, {
      color: stageColor('downscale'),
      size: Number(requiredElement('[data-stage-size="downscale"]', HTMLInputElement).value),
    });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      const outputFilePath = artifactPath(output);
      zip.file(
        `${root}/${outputFilePath}`,
        await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
      );
      materializedPaths.push(outputFilePath);
    }
    recipeDocuments.push({ stage: 'downscaled', path: 'recipes/downscaled.json', recipe });
  }
  if (stageEnabled('foreground-alpha')) {
    collectionStatus.textContent = 'compiling foreground-alpha recipe...';
    const recipe = createForegroundAlphaRecipe(REFERENCE_SET_ARCHIVE, { color: stageColor('foreground-alpha') });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      const outputFilePath = artifactPath(output);
      zip.file(
        `${root}/${outputFilePath}`,
        await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
      );
      materializedPaths.push(outputFilePath);
    }
    recipeDocuments.push({ stage: 'foreground-alpha', path: 'recipes/foreground-alpha.json', recipe });
  }
  if (stageEnabled('foreground-composites')) {
    collectionStatus.textContent = 'compiling foreground-composite recipe...';
    const recipe = createForegroundCompositeRecipe(REFERENCE_SET_ARCHIVE, { color: stageColor('foreground-composites') });
    for (const output of (await compileFamilyRecipe(recipe)).outputs) {
      const outputFilePath = artifactPath(output);
      zip.file(
        `${root}/${outputFilePath}`,
        await canvasBlob(canvasFromPixels(output.image.pixels, output.image.width, output.image.height)),
      );
      materializedPaths.push(outputFilePath);
    }
    recipeDocuments.push({ stage: 'foreground-composites', path: 'recipes/foreground-composites.json', recipe });
  }
  if (stageEnabled('historic-gradients')) {
    collectionStatus.textContent = 'compiling gradient-variant recipe...';
    await emitRecipe(
      'gradient-variants',
      'recipes/gradient-variants.json',
      createRasterFamilyRecipe(REFERENCE_SET_ARCHIVE, 'gradient-variants'),
      { materialize: false },
    );
  }
  if (stageEnabled('elliptical-gradients')) {
    collectionStatus.textContent = 'compiling elliptical-gradient recipe...';
    await emitRecipe(
      'elliptical-gradients',
      'recipes/elliptical-gradients.json',
      createOrganicGradientRecipe(REFERENCE_SET_ARCHIVE, {
        color: stageColor('elliptical-gradients'),
      }),
    );
  }
  if (stageEnabled('layer-compositions')) {
    collectionStatus.textContent = 'compiling layer-composition recipe...';
    await emitRecipe(
      'layer-compositions',
      'recipes/layer-compositions.json',
      createRasterFamilyRecipe(REFERENCE_SET_ARCHIVE, 'layer-compositions'),
      { materialize: false },
    );
  }
  if (stageEnabled('sans-glyphs')) {
    collectionStatus.textContent = 'compiling sans-glyph recipe...';
    await emitRecipe(
      'sans-glyphs',
      'recipes/sans-glyphs.json',
      createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
        familyId: 'sans-glyphs',
        sourceForeground: '#000000',
        sourceBackground: '#ffffff',
        foreground: stageColor('sans-glyphs'),
        background: requiredElement('[data-stage-background="sans-glyphs"]', HTMLInputElement).value,
      }),
    );
  }
  if (stageEnabled('serif-glyphs')) {
    collectionStatus.textContent = 'compiling serif-glyph recipe...';
    await emitRecipe(
      'serif-glyphs',
      'recipes/serif-glyphs.json',
      createTwoColorFamilyRecipe(REFERENCE_SET_ARCHIVE, {
        familyId: 'serif-glyphs',
        sourceForeground: '#ff0000',
        sourceBackground: '#0000ff',
        foreground: stageColor('serif-glyphs'),
        background: requiredElement('[data-stage-background="serif-glyphs"]', HTMLInputElement).value,
      }),
    );
  }
  if (stageEnabled('numbered-results')) {
    collectionStatus.textContent = 'compiling ordered-result recipe...';
    await emitRecipe('ordered-results', 'recipes/ordered-results.json', createOrderedResultsRecipe(REFERENCE_SET_ARCHIVE), {
      materialize: false,
    });
  }
  const manifest: CollectionManifest = {
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
  const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 6 } }, ({ percent }) => {
    collectionStatus.textContent = `compressing ${Math.round(percent)}%`;
  });
  download('bin-block-collection.zip', blob);
  collectionStatus.textContent = `${imageCount} images downloaded`;
}

requiredElement('#download-collection', HTMLButtonElement).addEventListener('click', async () => {
  try {
    await buildCollection();
  } catch (error: unknown) {
    collectionStatus.textContent = errorMessage(error);
  }
});

paletteGrid.innerHTML = DEFAULT_PALETTE.map(
  ([name, colour]) =>
    `<button class="swatch selected" type="button" style="--colour: ${colour}" data-name="${name}" data-colour="${colour}" aria-pressed="true"><span></span><code>${colour}</code></button>`,
).join('');
paletteGrid.addEventListener('click', (event) => {
  if (!(event.target instanceof Element)) return;
  const button = event.target.closest<HTMLButtonElement>('[data-colour]');
  if (!button) return;
  button.classList.toggle('selected');
  button.setAttribute('aria-pressed', String(button.classList.contains('selected')));
  updateCollectionCount();
});
requiredElement('#preset-list', HTMLElement).addEventListener('change', updateCollectionCount);
requiredElement('#custom-colours', HTMLInputElement).addEventListener('input', updateCollectionCount);
requiredElement('#stage-list', HTMLElement).addEventListener('change', updateCollectionCount);
requiredElement('#load-config', HTMLButtonElement).addEventListener('click', loadConfigurationEditor);
requiredElement('#apply-config', HTMLButtonElement).addEventListener('click', async () => {
  try {
    applyCollectionConfiguration();
    await updateCollectionCount();
    collectionStatus.textContent = 'configuration applied';
  } catch (error: unknown) {
    collectionStatus.textContent = errorMessage(error);
  }
});

updateCollectionCount();
loadConfigurationEditor();
