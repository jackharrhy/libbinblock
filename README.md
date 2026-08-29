# libbinblock

`libbinblock` is a portable C11 BinScript compiler, immutable semantic graph,
lazy artifact planner, backend planner, and canonical RGBA8 software renderer.
The browser notebook uses the library through WebAssembly and optionally
validates supported previews through WebGL2; the native CLI uses the same
compiler and rasterizer.

The original TypeScript compiler and recipe executor live under `legacy-ts/` as
a behavioral oracle only and are not part of the production bundle. See the
[build instructions](docs/building.md), [implementation status](docs/implementation-status.md),
the [gate-by-gate compliance matrix](docs/lib-plan-compliance.md), and the
[historical architecture plan](LIB_PLAN.md).

The complete lazy reference program is generated at
`reference-set/reference-set.binscript`. Run `npm run conformance:reference` to
compare all 4,312 outputs and produce aggregate and per-file reports.

The browser registers the reference manifest as asset metadata, compiles the
complete program without downloading the PNG archive, and hydrates only the
assets needed by the requested preview window. Downloaded PNGs are checked by
encoded SHA-256 and decoded with the same straight-alpha RGBA8 contract used by
the manifest, including RGB stored beneath zero alpha.

A **bin**block **gen**erator.

## Where to start

| Goal                                | Entry point                                                                                                                             |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Embed the C library                 | [`include/binblock/binblock.h`](include/binblock/binblock.h), then link the `binblock::binblock` CMake target                           |
| Learn BinScript                     | [`BINSCRIPT.md`](BINSCRIPT.md) and [`examples/starter.binscript`](examples/starter.binscript)                                           |
| Use the native CLI                  | [`cli/main.c`](cli/main.c) and the commands below                                                                                       |
| Work on the browser notebook        | [`apps/browser/app.ts`](apps/browser/app.ts); `npm run build` is orchestrated by [`scripts/build-browser.ts`](scripts/build-browser.ts) |
| Use an engine integration           | [Godot GDExtension](integrations/godot/README.md) or [Wii/GX adapter](integrations/wii/README.md)                                       |
| Understand implementation decisions | [`docs/decisions/`](docs/decisions/) and [`docs/implementation-status.md`](docs/implementation-status.md)                               |
| Validate a change                   | `npm run verify`                                                                                                                        |

## Repository layout

The production dependency direction is intentionally simple: public headers in
`include/binblock/` describe the API, `lib/` implements it, and the CLI,
bindings, apps, and integrations consume it.

| Path                | Owns                                                                                                              |
| ------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `include/binblock/` | Installed public C API, including the Wasm host ABI declarations                                                  |
| `lib/`              | Portable C11 implementation, grouped by core, frontend, semantic, raster, backend, and reference-profile concerns |
| `cli/`              | Native `binblock` executable and its PNG/hash adapters                                                            |
| `bindings/`         | Generic JavaScript runtime wrapper, WebGL2 lowering, and the Emscripten C ABI                                     |
| `apps/browser/`     | Production CodeMirror/Wasm notebook and reference-corpus asset host                                               |
| `integrations/`     | Consumer adapters and runnable Godot and Wii demos                                                                |
| `examples/`         | Small BinScript programs suitable for the CLI                                                                     |
| `tests/`            | Current native, language, fuzz, TypeScript, browser-Wasm, and integration tests                                   |
| `scripts/`          | Build, inventory, benchmark, and generated-contract tooling                                                       |
| `reference-set/`    | Checked-in 4,312-PNG conformance corpus plus its generated manifest, BinScript program, and TSV contract          |
| `legacy-ts/`        | Retired TypeScript implementation, its tests, and its historical JSON recipe documentation; oracle only           |
| `docs/`             | Build guidance, decisions, status, render profiles, and reference-corpus rationale                                |

The large-by-file-count `reference-set/` directory is test source data, not a
build output. Generated local output belongs in ignored directories such as
`.build/`, `dist/`, and `node_modules/`; Godot and Wii demo build products are
ignored as well. See [`reference-set/README.md`](reference-set/README.md) before
changing the corpus or its generated contracts.

## Run locally

Install Node.js and activate an Emscripten SDK, then run:

```sh
npm install
npm run build
python3 -m http.server 8888
```

Open `http://localhost:8888/dist/`.

For native library/CLI development without Emscripten, use `npm run test:c`.

## Native CLI

`npm run build:c` creates `.build/native/binblock`. The host can check source,
inspect a stable graph dump, enumerate lazy outputs, render/package bounded
ranges, precompile BBM modules, inventory fixtures, and compare against an
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
