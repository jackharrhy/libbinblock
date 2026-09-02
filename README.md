# libbinblock

`libbinblock` is a portable C11 BinScript compiler, immutable semantic graph,
lazy artifact evaluator, and canonical RGBA8 software renderer.
The browser notebook uses the library through WebAssembly and optionally
validates supported previews through WebGL2; the native CLI uses the same
compiler and rasterizer.

See the [build instructions](docs/building.md),
[implementation status](docs/implementation-status.md),
[architecture decisions](docs/decisions/), and [BinScript guide](BINSCRIPT.md).

The browser's **Generated set** example is a small, asset-free BinScript program:
it combines a palette with analytic layers through a lazy product and renders
the results with the same C/Wasm compiler used by the CLI. The larger checked-in
reference corpus is kept out of the website and remains a developer conformance
oracle. Run `npm run conformance:reference` when a change needs comparison
against that archive.

A **bin**block **gen**erator.

## Where to start

| Goal                                 | Entry point                                                                                                                                        |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| Embed the C library                  | [`include/binblock/binblock.h`](include/binblock/binblock.h), then link the `binblock::binblock` CMake target                                      |
| Learn BinScript                      | [`BINSCRIPT.md`](BINSCRIPT.md) and [`examples/starter.binscript`](examples/starter.binscript)                                                      |
| Use the native CLI                   | [`cli/main.c`](cli/main.c) and the commands below                                                                                                  |
| Work on the browser portal           | [`apps/browser/main.tsx`](apps/browser/main.tsx), with the notebook engine in [`apps/browser/c-wasm-notebook.ts`](apps/browser/c-wasm-notebook.ts) |
| Use a graphics or engine integration | [SDL GPU](integrations/sdl_gpu/README.md), [Godot GDExtension](integrations/godot/README.md), or [Wii/GX](integrations/wii/README.md)              |
| Understand implementation decisions  | [`docs/decisions/`](docs/decisions/) and [`docs/implementation-status.md`](docs/implementation-status.md)                                          |
| Validate a change                    | `npm run verify`                                                                                                                                   |

## Repository layout

The production dependency direction is intentionally simple: public headers in
`include/binblock/` describe the API, `lib/` implements it, and the CLI,
bindings, apps, and integrations consume it.

| Path                | Owns                                                                                                     |
| ------------------- | -------------------------------------------------------------------------------------------------------- |
| `include/binblock/` | Installed public C API, including the Wasm host ABI declarations                                         |
| `lib/`              | Portable C11 implementation, grouped by core, frontend, semantic, raster, and reference-profile concerns |
| `cli/`              | Native `binblock` executable and its PNG/hash adapters                                                   |
| `bindings/`         | Generic JavaScript runtime wrapper, WebGL2 lowering, and the Emscripten C ABI                            |
| `apps/browser/`     | React developer portal, CodeMirror/Wasm playground, and compact native generated-set example             |
| `integrations/`     | Consumer adapters and runnable Godot and Wii demos                                                       |
| `examples/`         | Small BinScript programs suitable for the CLI                                                            |
| `tests/`            | Current native, language, fuzz, TypeScript, browser-Wasm, and integration tests                          |
| `scripts/`          | Wasm staging, inventory, benchmark, and generated-contract tooling                                       |
| `reference-set/`    | Checked-in 4,312-PNG conformance corpus plus its generated manifest, BinScript program, and TSV contract |
| `docs/`             | Build guidance, decisions, status, render profiles, and reference-corpus rationale                       |

The large-by-file-count `reference-set/` directory is test source data, not a
build output. Generated local output belongs in ignored directories such as
`.build/`, `dist/`, and `node_modules/`; Godot and Wii demo build products are
ignored as well. See [`reference-set/README.md`](reference-set/README.md) before
changing the corpus or its generated contracts.

## Run locally

Install Node.js and activate an Emscripten SDK, then run:

```sh
npm install
npm run build:wasm
npm run dev
```

Open `http://localhost:8888/`. The Vite server provides route fallback and hot
module replacement. `npm run build` emits the production portal to `dist/`.

For native library/CLI development without Emscripten, use `npm run test:c`.

## Native CLI

`npm run build:c` creates `.build/native/binblock`. The host can check source,
inspect a stable graph dump, enumerate lazy outputs, render/package bounded
ranges, inventory fixtures, and compare against an
executable conformance contract:

```sh
.build/native/binblock check examples/starter.binscript
.build/native/binblock graph tests/language/golden-starter.binscript
.build/native/binblock list reference-set/reference-set.binscript --summary
.build/native/binblock render examples/preview-304.binscript --start 100 --count 16 --dir /tmp/binblock-preview
npm run conformance:reference
```

Run `npm run verify` for the deterministic inventory, generated-program,
TypeScript host, native unit, language-golden, and fast conformance gates. The
sanitizer, Wasm, fuzz, full-conformance, Godot, Wii, release, and 32-bit jobs are
defined in `.github/workflows/ci.yml`.
