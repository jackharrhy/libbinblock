import { useEffect, useRef, useState } from 'react';

import { BINBLOCK_NOTEBOOK_STORAGE_KEY, createCWasmNotebook, type CWasmNotebook } from '../c-wasm-notebook.js';

export function PlaygroundPage() {
  const editor = useRef<HTMLDivElement>(null);
  const preview = useRef<HTMLElement>(null);
  const status = useRef<HTMLDivElement>(null);
  const parameters = useRef<HTMLDivElement>(null);
  const generatedSetButton = useRef<HTMLButtonElement>(null);
  const renderAllButton = useRef<HTMLButtonElement>(null);
  const exportButton = useRef<HTMLButtonElement>(null);
  const previewStart = useRef<HTMLInputElement>(null);
  const [failure, setFailure] = useState<string>();

  useEffect(() => {
    if (!editor.current || !preview.current || !status.current || !parameters.current) return;
    let disposed = false;
    let notebook: CWasmNotebook | undefined;

    void createCWasmNotebook({
      parent: editor.current,
      preview: preview.current,
      status: status.current,
      parameters: parameters.current,
      generatedSetButton: generatedSetButton.current ?? undefined,
      renderAllButton: renderAllButton.current ?? undefined,
      exportButton: exportButton.current ?? undefined,
      previewStart: previewStart.current ?? undefined,
      initialSource: localStorage.getItem(BINBLOCK_NOTEBOOK_STORAGE_KEY) ?? undefined,
    })
      .then((created) => {
        if (disposed) created.dispose();
        else notebook = created;
      })
      .catch((error: unknown) => {
        if (!disposed) setFailure(error instanceof Error ? error.message : String(error));
      });

    return () => {
      disposed = true;
      notebook?.dispose();
    };
  }, []);

  return (
    <section className="notebook-app" aria-label="BinScript editor">
      <header className="notebook-bar">
        <div className="notebook-heading">
          <strong>Editor</strong>
          <span>C compiler via WebAssembly</span>
        </div>
        <button ref={generatedSetButton} type="button">
          Generated set
        </button>
        <button ref={renderAllButton} type="button" disabled>
          Render all
        </button>
        <label className="notebook-preview-start">
          <span>Start index</span>
          <input ref={previewStart} type="number" min="0" step="1" defaultValue="0" />
        </label>
        <button ref={exportButton} type="button">
          Export
        </button>
      </header>
      {failure ? (
        <div className="notebook-failure" role="alert">
          <strong>The editor could not start.</strong>
          <span>{failure}</span>
        </div>
      ) : (
        <>
          <div className="notebook-runtime-status" ref={status}>
            Loading the compiler...
          </div>
          <div className="notebook-parameters" ref={parameters} aria-label="BinScript parameters" />
          <div className="notebook-workspace">
            <div ref={editor} className="binscript-editor" aria-label="BinScript recipe editor" />
            <aside className="notebook-preview" ref={preview} aria-label="Generated previews" />
          </div>
        </>
      )}
    </section>
  );
}
