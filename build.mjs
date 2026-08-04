import * as esbuild from 'esbuild';
import { createCanvas, loadImage } from 'canvas';
import { createHash } from 'node:crypto';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { join, relative, sep } from 'node:path';
import { createDefaultSetRasterRecipe } from './src/default-set-recipe.js';
import { parseRecipeDocument } from './src/recipe-schema.js';

async function walkPngs(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const paths = await Promise.all(entries.map((entry) => {
    const path = join(directory, entry.name);
    return entry.isDirectory() ? walkPngs(path) : entry.name.toLowerCase().endsWith('.png') ? [path] : [];
  }));
  return paths.flat();
}

async function archivePayload() {
  const root = join(process.cwd(), 'default-set');
  const paths = (await walkPngs(root)).sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
  const files = await Promise.all(paths.map(async (path) => {
    const bytes = await readFile(path);
    return {
      path: relative(root, path).split(sep).join('/'),
      base64: bytes.toString('base64'),
      sha256: `sha256:${createHash('sha256').update(bytes).digest('hex')}`,
    };
  }));
  const grouped = new Map();
  for (const file of files) {
    const group = file.path.split('/')[0];
    if (!grouped.has(group)) grouped.set(group, []);
    grouped.get(group).push(file);
  }
  const columns = 64;
  const tileSize = 64;
  const groups = [...grouped].map(([name, groupFiles]) => ({ name, count: groupFiles.length, files: groupFiles }));
  const rows = groups.reduce((total, group) => total + Math.ceil(group.count / columns) + 1, 0) - 1;
  const atlas = createCanvas(columns * tileSize, rows * tileSize);
  const context = atlas.getContext('2d');
  context.imageSmoothingEnabled = false;
  let row = 0;
  for (const group of groups) {
    group.atlasRow = row;
    group.atlasRows = Math.ceil(group.count / columns);
    for (let offset = 0; offset < group.files.length; offset += 1) {
      const image = await loadImage(Buffer.from(group.files[offset].base64, 'base64'));
      const scale = Math.min(1, tileSize / image.width, tileSize / image.height);
      context.drawImage(image, (offset % columns) * tileSize, (row + Math.floor(offset / columns)) * tileSize, image.width * scale, image.height * scale);
    }
    row += group.atlasRows + 1;
  }
  return {
    files,
    groups: groups.map(({ name, count, atlasRow, atlasRows }) => ({ name, count, atlasRow, atlasRows })),
    atlas: { width: atlas.width, height: atlas.height, columns, base64: atlas.toBuffer('image/png').toString('base64') },
  };
}

const archive = await archivePayload();
const defaultSetRecipe = parseRecipeDocument(createDefaultSetRasterRecipe(archive));
if (defaultSetRecipe.metadata.imageCount !== archive.files.length) throw new Error('The default-set recipe does not cover the complete archive.');

const result = await esbuild.build({
  entryPoints: ['src/app.js'],
  bundle: true,
  format: 'iife',
  target: 'es2022',
  outdir: '.build',
  write: false,
  logLevel: 'info',
  plugins: [{
    name: 'embedded-default-set-archive',
    setup(build) {
      build.onResolve({ filter: /^virtual:default-set-archive$/ }, () => ({ path: 'archive', namespace: 'default-set' }));
      build.onLoad({ filter: /.*/, namespace: 'default-set' }, () => ({
        contents: `export const DEFAULT_SET_ARCHIVE = ${JSON.stringify(archive)};`,
        loader: 'js',
      }));
    },
  }],
});

const script = result.outputFiles.find((file) => file.path.endsWith('.js'))?.text;
const styles = result.outputFiles.find((file) => file.path.endsWith('.css'))?.text;

if (!script || !styles) throw new Error('Expected JavaScript and CSS bundles.');

const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="description" content="A browser tool for previewing and exporting bin block image collections.">
  <link rel="icon" href="data:,">
  <title>Bingen</title>
  <style>${styles}</style>
</head>
<body>
  <main id="app"></main>
  <script>${script}</script>
</body>
</html>`;

await mkdir('dist', { recursive: true });
await writeFile('dist/index.html', html);
