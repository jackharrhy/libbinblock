import { createHash } from 'node:crypto';
import { readdir, readFile, writeFile } from 'node:fs/promises';
import { join, relative } from 'node:path';

const archiveRoot = join(process.cwd(), 'reference-set');
const reportPath = join(process.cwd(), 'DUPLICATES.md');

async function walk(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const paths = await Promise.all(entries.map(async (entry) => {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) return walk(path);
    return entry.name.toLowerCase().endsWith('.png') ? [path] : [];
  }));
  return paths.flat();
}

const paths = await walk(archiveRoot);
const groups = new Map();
for (const path of paths) {
  const bytes = await readFile(path);
  const hash = createHash('md5').update(bytes).digest('hex');
  groups.set(hash, [...(groups.get(hash) ?? []), relative(archiveRoot, path)]);
}

const duplicates = [...groups.entries()]
  .filter(([, files]) => files.length > 1)
  .sort(([, left], [, right]) => right.length - left.length || left[0].localeCompare(right[0]));
const duplicateFiles = duplicates.reduce((total, [, files]) => total + files.length, 0);
const report = [
  '# Default Set Exact Duplicate Report',
  '',
  `Generated: ${new Date().toISOString()}`,
  '',
  `- PNG files hashed: ${paths.length}`,
  `- Unique PNG byte streams: ${groups.size}`,
  `- Duplicate hash groups: ${duplicates.length}`,
  `- Files in duplicate groups: ${duplicateFiles}`,
  '',
  'An exact duplicate means the entire PNG byte stream has the same MD5 hash. Visually identical images encoded differently are not included and need a future pixel-hash pass.',
  '',
  ...duplicates.flatMap(([hash, files]) => [
    `## ${files.length} identical files: \`${hash}\``,
    '',
    ...files.map((file) => `- \`${file}\``),
    '',
  ]),
].join('\n');

await writeFile(reportPath, report);
console.log(`Hashed ${paths.length} PNGs. Found ${duplicates.length} duplicate groups in ${reportPath}.`);
