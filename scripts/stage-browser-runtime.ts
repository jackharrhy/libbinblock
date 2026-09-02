import { copyFile, mkdir, rm } from 'node:fs/promises';

const sourceDirectory = '.build/wasm';
const destinationDirectory = '.build/browser-public';

await mkdir(destinationDirectory, { recursive: true });
await rm(`${destinationDirectory}/reference-set`, { recursive: true, force: true });

try {
  await Promise.all([
    copyFile(`${sourceDirectory}/binblock.mjs`, `${destinationDirectory}/binblock.mjs`),
    copyFile(`${sourceDirectory}/binblock.wasm`, `${destinationDirectory}/binblock.wasm`),
  ]);
} catch (error) {
  throw new Error('Wasm browser artifacts are missing. Activate Emscripten and run npm run build:wasm first.', {
    cause: error,
  });
}
