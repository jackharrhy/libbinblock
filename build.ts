import * as esbuild from 'esbuild';
import { createCanvas, loadImage } from 'canvas';
import { createHash } from 'node:crypto';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { join, relative, sep } from 'node:path';
import { createReferenceSetRasterRecipe } from './src/reference-set-recipe.js';
import { parseRecipeDocument } from './src/recipe-schema.js';

type ArchiveFile = {
  path: string;
  base64: string;
  sha256: string;
};

type ArchiveGroup = {
  name: string;
  count: number;
  files: ArchiveFile[];
  atlasRow: number;
  atlasRows: number;
};

type ArchivePayload = {
  files: ArchiveFile[];
  groups: Omit<ArchiveGroup, 'files'>[];
  atlas: {
    width: number;
    height: number;
    columns: number;
    base64: string;
  };
};

async function walkPngs(directory: string): Promise<string[]> {
  const entries = await readdir(directory, { withFileTypes: true });
  const paths: string[][] = await Promise.all(
    entries.map((entry): Promise<string[]> | string[] => {
      const path = join(directory, entry.name);
      return entry.isDirectory() ? walkPngs(path) : entry.name.toLowerCase().endsWith('.png') ? [path] : [];
    }),
  );
  return paths.flat();
}

async function archivePayload(): Promise<ArchivePayload> {
  const root = join(process.cwd(), 'reference-set');
  const paths = (await walkPngs(root)).sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
  const files: ArchiveFile[] = await Promise.all(
    paths.map(async (path): Promise<ArchiveFile> => {
      const bytes = await readFile(path);
      return {
        path: relative(root, path).split(sep).join('/'),
        base64: bytes.toString('base64'),
        sha256: `sha256:${createHash('sha256').update(bytes).digest('hex')}`,
      };
    }),
  );
  const grouped = new Map<string, ArchiveFile[]>();
  for (const file of files) {
    const group = file.path.split('/')[0];
    let groupFiles = grouped.get(group);
    if (!groupFiles) {
      groupFiles = [];
      grouped.set(group, groupFiles);
    }
    groupFiles.push(file);
  }
  const columns = 64;
  const tileSize = 64;
  const groups: ArchiveGroup[] = [...grouped].map(([name, groupFiles]): ArchiveGroup => ({
    name,
    count: groupFiles.length,
    files: groupFiles,
    atlasRow: 0,
    atlasRows: 0,
  }));
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
      context.drawImage(
        image,
        (offset % columns) * tileSize,
        (row + Math.floor(offset / columns)) * tileSize,
        image.width * scale,
        image.height * scale,
      );
    }
    row += group.atlasRows + 1;
  }
  return {
    files,
    groups: groups.map(({ name, count, atlasRow, atlasRows }) => ({ name, count, atlasRow, atlasRows })),
    atlas: { width: atlas.width, height: atlas.height, columns, base64: atlas.toBuffer('image/png').toString('base64') },
  };
}

const archive: ArchivePayload = await archivePayload();
const referenceSetRecipe = parseRecipeDocument(createReferenceSetRasterRecipe(archive));
if (referenceSetRecipe.metadata.imageCount !== archive.files.length)
  throw new Error('The reference-set recipe does not cover the complete archive.');

const blueHighVariantSlice = [
  'col_blue_hi-grad00blk100.png',
  'col_blue_hi-grad00blk25.png',
  'col_blue_hi-grad00wht.png',
  'col_blue_hi-grad01blk100.png',
  'col_blue_hi-grad01blk25.png',
  'col_blue_hi-grad01wht.png',
  'col_blue_hi-grad02blk100.png',
  'col_blue_hi-grad02blk50.png',
  'col_blue_hi-grad02wht.png',
  'col_blue_hi-grad03blk100.png',
  'col_blue_hi-grad03blk25.png',
  'col_blue_hi-grad03blk50.png',
  'col_blue_hi-grad03wht100.png',
  'col_blue_hi-grad04blk100.png',
  'col_blue_hi-grad04blk50.png',
  'col_blue_hi-grad04wht.png',
];
const archiveFilesByPath = new Map(archive.files.map((file) => [file.path, file]));
const comparisonFixtures = {
  'gradient-masks': archive.files
    .filter((file) => file.path.startsWith('Gradient Layers Alpha Maps/'))
    .map(({ path, base64 }) => ({
      key: path
        .split('/')
        .at(-1)
        ?.replace(/\.png$/i, ''),
      base64,
    })),
  'gradient-variants-blue-hi': blueHighVariantSlice.map((filename) => {
    const file = archiveFilesByPath.get(`col bin 2/rgb/${filename}`);
    if (!file) throw new Error(`Missing comparison fixture: ${filename}`);
    return { key: filename.replace(/\.png$/i, ''), base64: file.base64 };
  }),
};

const result: esbuild.BuildResult = await esbuild.build({
  entryPoints: ['src/app.ts'],
  bundle: true,
  format: 'iife',
  target: 'es2022',
  outdir: '.build',
  write: false,
  logLevel: 'info',
  plugins: [
    {
      name: 'embedded-reference-set-archive',
      setup(build: esbuild.PluginBuild): void {
        build.onResolve({ filter: /^virtual:reference-set-archive$/ }, (): esbuild.OnResolveResult => ({
          path: 'archive',
          namespace: 'reference-set',
        }));
        build.onLoad({ filter: /.*/, namespace: 'reference-set' }, (): esbuild.OnLoadResult => ({
          contents: `export const REFERENCE_SET_ARCHIVE = ${JSON.stringify(archive)};`,
          loader: 'js',
        }));
        build.onResolve({ filter: /^virtual:comparison-fixtures$/ }, (): esbuild.OnResolveResult => ({
          path: 'fixtures',
          namespace: 'comparison-fixtures',
        }));
        build.onLoad({ filter: /.*/, namespace: 'comparison-fixtures' }, (): esbuild.OnLoadResult => ({
          contents: `export const COMPARISON_FIXTURES = ${JSON.stringify(comparisonFixtures)};`,
          loader: 'js',
        }));
      },
    },
  ],
});

const script: string | undefined = result.outputFiles?.find((file: esbuild.OutputFile): boolean => file.path.endsWith('.js'))?.text;
const styles: string | undefined = result.outputFiles?.find((file: esbuild.OutputFile): boolean => file.path.endsWith('.css'))?.text;

if (!script || !styles) throw new Error('Expected JavaScript and CSS bundles.');

const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="description" content="A live BinScript notebook for composing deterministic image collections.">
  <link rel="icon" href="data:,">
  <title>BinScript · Bingen</title>
  <style>${styles}</style>
</head>
<body>
  <main id="app"></main>
  <script>${script}</script>
</body>
</html>`;

await mkdir('dist', { recursive: true });
await writeFile('dist/index.html', html);
