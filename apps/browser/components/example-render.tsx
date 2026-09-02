import { useEffect, useRef, useState } from 'react';

import {
  createBinBlockRuntime,
  type BinBlockImage,
  type BinBlockRuntime,
  type BinBlockWasmFactory,
} from '../../../bindings/javascript/binblock.js';
import { binScriptExamples, type BinScriptExampleId } from '../binscript-examples.js';

interface RenderedArtifact {
  image: BinBlockImage;
  key: string;
}

export interface ExampleRenderResult {
  artifacts?: readonly RenderedArtifact[];
  failure?: string;
}

function artifactLabel(example: BinScriptExampleId, output: number, key: string): string {
  if (example === 'collections') return (output === 0 ? 'solid ' : 'masked ') + key;
  if (example === 'gradient') return 'sky';
  if (example === 'mask') return 'masked ink';
  return 'base + light';
}

function ArtifactCanvas({ artifact }: { artifact: RenderedArtifact }) {
  const canvas = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const context = canvas.current?.getContext('2d');
    if (!context) return;
    context.putImageData(new ImageData(Uint8ClampedArray.from(artifact.image.pixels), artifact.image.width, artifact.image.height), 0, 0);
  }, [artifact]);

  return (
    <figure>
      <canvas
        ref={canvas}
        width={artifact.image.width}
        height={artifact.image.height}
        aria-label={artifact.key + ', ' + artifact.image.width + ' by ' + artifact.image.height + ' pixels'}
      />
      <figcaption>{artifact.key}</figcaption>
    </figure>
  );
}

export function useExampleRenders(): Partial<Record<BinScriptExampleId, ExampleRenderResult>> {
  const [results, setResults] = useState<Partial<Record<BinScriptExampleId, ExampleRenderResult>>>({});

  useEffect(() => {
    let disposed = false;
    let runtime: BinBlockRuntime | undefined;

    void (async () => {
      const moduleUrl = new URL('/binblock.mjs', window.location.href).href;
      const { default: createBinBlockModule } = (await import(/* @vite-ignore */ moduleUrl)) as {
        default: BinBlockWasmFactory;
      };
      runtime = await createBinBlockRuntime(createBinBlockModule);
      if (disposed) {
        runtime.dispose();
        return;
      }

      const next: Partial<Record<BinScriptExampleId, ExampleRenderResult>> = {};
      for (const example of binScriptExamples) {
        try {
          const diagnostics = runtime.compile(example.source);
          const errors = diagnostics.filter((diagnostic) => diagnostic.severity === 2);
          if (errors.length > 0) throw new Error(errors.map((diagnostic) => diagnostic.message).join('\n'));

          const rendered: RenderedArtifact[] = [];
          for (const output of runtime.outputs()) {
            const count = Number(output.cardinality);
            if (!Number.isSafeInteger(count) || count > 32) throw new RangeError('The example produced too many artifacts.');
            const artifacts = runtime.artifacts(output.index, 0n, count);
            for (let index = 0; index < artifacts.length; index += 1) {
              rendered.push({
                key: artifactLabel(example.id, output.index, artifacts[index].key),
                image: await runtime.render(output.index, BigInt(index)),
              });
            }
          }
          next[example.id] = { artifacts: rendered };
        } catch (error) {
          next[example.id] = { failure: error instanceof Error ? error.message : String(error) };
        }
      }
      if (!disposed) setResults(next);
    })().catch((error: unknown) => {
      if (disposed) return;
      const failure = error instanceof Error ? error.message : String(error);
      setResults(Object.fromEntries(binScriptExamples.map((example) => [example.id, { failure }])));
    });

    return () => {
      disposed = true;
      runtime?.dispose();
    };
  }, []);

  return results;
}

export function ExampleRender({ result, caption }: { result?: ExampleRenderResult; caption: string }) {
  return (
    <figure className={'example-render' + (result?.artifacts?.length === 1 ? ' example-render--single' : '')}>
      {result?.failure ? (
        <p className="example-render-status" role="alert">
          The browser renderer could not load: {result.failure}
        </p>
      ) : result?.artifacts ? (
        <div className="example-output-grid">
          {result.artifacts.map((artifact, index) => (
            <ArtifactCanvas key={artifact.key + '-' + index} artifact={artifact} />
          ))}
        </div>
      ) : (
        <p className="example-render-status">Loading the C compiler and renderer...</p>
      )}
      <figcaption>{caption}</figcaption>
    </figure>
  );
}
