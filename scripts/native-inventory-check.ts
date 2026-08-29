import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

interface NativeInventory {
  format: string;
  fileCount: number;
  files: unknown[];
}

const repository = resolve(import.meta.dirname, '..');
const executable = join(repository, '.build/native/binblock');
const temporary = await mkdtemp(join(tmpdir(), 'binblock-native-inventory-'));
const first = join(temporary, 'first.json');
const second = join(temporary, 'second.json');

function inventory(output: string): void {
  const result = spawnSync(executable, ['inventory', 'reference-set', '--out', output], {
    cwd: repository,
    encoding: 'utf8',
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(result.stderr || result.stdout || 'Native inventory failed.');
}

try {
  inventory(first);
  inventory(second);
  const [firstBytes, secondBytes, referenceBytes] = await Promise.all([
    readFile(first),
    readFile(second),
    readFile(join(repository, 'reference-manifest.json')),
  ]);
  if (!firstBytes.equals(secondBytes)) throw new Error('Native inventory output is not byte-deterministic.');
  const native = JSON.parse(firstBytes.toString('utf8')) as NativeInventory;
  const reference = JSON.parse(referenceBytes.toString('utf8')) as NativeInventory;
  if (native.format !== 'binblock-inventory/v1' || native.fileCount !== 4312)
    throw new Error('Native inventory header or file count is invalid.');
  if (JSON.stringify(native.files) !== JSON.stringify(reference.files))
    throw new Error('Native inventory metadata differs from the locked reference manifest.');
  console.log('Native CLI inventory is byte-deterministic and matches all 4,312 locked records.');
} finally {
  await rm(temporary, { recursive: true, force: true });
}
