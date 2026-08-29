# Bingen / libbinblock

Bingen is now a browser host for `libbinblock`, a portable C11 BinScript
compiler, immutable semantic graph, lazy artifact planner, backend planner, and
canonical RGBA8 software renderer. The production notebook uses that library
through WebAssembly and optionally validates supported previews through WebGL2;
the native CLI uses the same compiler and rasterizer.

The original TypeScript compiler and recipe executor live under `legacy-ts/` as
a behavioral oracle only and are not part of the production bundle. See the
[build instructions](docs/building.md), [implementation status](docs/implementation-status.md),
the [gate-by-gate compliance matrix](docs/lib-plan-compliance.md), and the
[architecture plan](LIB_PLAN.md).

The complete lazy reference program is generated at
`reference-set/reference-set.binscript`. Run `npm run conformance:reference` to
compare all 4,312 outputs and produce aggregate and per-file reports.

The browser registers the reference manifest as asset metadata, compiles the
complete program without downloading the PNG archive, and hydrates only the
assets needed by the requested preview window. Downloaded PNGs are checked by
encoded SHA-256 and decoded with the same straight-alpha RGBA8 contract used by
the manifest, including RGB stored beneath zero alpha.

A **bin**block **gen**erator.

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
