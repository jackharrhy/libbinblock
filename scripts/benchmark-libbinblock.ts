import { spawnSync } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { performance } from 'node:perf_hooks';

interface Measurement {
  name: string;
  iterations: number;
  totalMilliseconds: number;
  meanMilliseconds: number;
}

const repository = resolve(import.meta.dirname, '..');
const executable = join(repository, '.build/native/binblock');
const temporary = await mkdtemp(join(tmpdir(), 'binblock-benchmark-'));
const measurements: Measurement[] = [];

function run(arguments_: readonly string[]): void {
  const result = spawnSync(executable, arguments_, {
    cwd: repository,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`binblock ${arguments_.join(' ')} failed:\n${result.stderr || result.stdout}`);
}

function measure(name: string, iterations: number, operation: () => void): void {
  operation();
  const started = performance.now();
  for (let iteration = 0; iteration < iterations; iteration += 1) operation();
  const totalMilliseconds = performance.now() - started;
  measurements.push({
    name,
    iterations,
    totalMilliseconds,
    meanMilliseconds: totalMilliseconds / iterations,
  });
}

try {
  measure('starter parse/check', 20, () => run(['check', 'examples/starter.binscript']));
  measure('enumerate 4,312 outputs', 5, () => run(['list', 'reference-set/reference-set.binscript', '--summary']));
  measure('64x64 gradient/composite', 20, () =>
    run(['render', 'examples/benchmark-gradient.binscript', '--count', '1', '--dir', join(temporary, 'gradient')]),
  );
  measure('304-plan 16-item preview', 5, () =>
    run(['render', 'examples/preview-304.binscript', '--start', '100', '--count', '16', '--dir', join(temporary, 'preview')]),
  );

  if (process.argv.includes('--full')) {
    measure('full reference package cold pass', 1, () =>
      run(['package', 'reference-set/reference-set.binscript', '--limit', '4312', '--dir', join(temporary, 'package-cold')]),
    );
    measure('full reference package warm filesystem pass', 1, () =>
      run(['package', 'reference-set/reference-set.binscript', '--limit', '4312', '--dir', join(temporary, 'package-warm')]),
    );
  }

  process.stdout.write(`${JSON.stringify({ format: 'binblock-benchmark/v1', measurements }, null, 2)}\n`);
} finally {
  await rm(temporary, { recursive: true, force: true });
}
