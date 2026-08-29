# LIB_PLAN implementation status

All architectural phases in `LIB_PLAN.md` have an implementation in this tree.
The important gates and their evidence are:

| Phase                | Implemented evidence                                                                                                                                                      |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Oracle               | Deterministic 4,312-file manifest, family counts, hashes, aliases, smoke fixtures, and per-file equivalence TSV                                                           |
| Portable foundation  | Conservative C11 CMake target, host allocator, limits, UTF-8, source spans, diagnostics, arenas, interning, warnings-as-errors, sanitizer option                          |
| Raster semantics     | Documented exact profile and canonical RGBA8 implementations for transforms, composition, masks, gradients/fields, assets, and Lanczos3                                   |
| Semantic graph       | Immutable typed nodes, structural hashes, provenance, validation, direct C construction, raster execution                                                                 |
| Collections          | Lazy bounded map/filter/flat-map/concat/zip/product/select/slice with checked cardinality and indexed access                                                              |
| BinScript            | Lossless syntax/recovery, semantic typing/lowering, built-ins, trace queries, host modules/assets, scalar overrides                                                       |
| CLI/conformance      | Check/list/render/package/precompile/compare commands and complete 4,312-output manifest conformance                                                                      |
| Wasm/notebook        | Generation-checked integer handles, copy-out ABI, metadata-only 4,312-item compile, graph-root asset hydration, bounded previews, C-owned production notebook             |
| Backends             | Capability model, graph partition planner, bounded CPU fallback, WebGL2 shader lowering and context-loss fallback                                                         |
| Godot                | Godot 4.5+ GDExtension/runtime gate, live `BinProgram` parameters, native-sized harsh-palette editor gallery, and Binblock-powered `TileMapLayer`/`CharacterBody2D` world |
| Wii                  | devkitPPC/libogc2 build, endian-stable BBM host, bounded enumeration, CPU bake, GX RGBA8 tiling/upload, and Dolphin-rendered native-sized harsh-palette gallery           |
| Prototype retirement | Old TypeScript compiler/executor moved to `legacy-ts/`; production `apps/browser/` contains only the Wasm notebook host                                                   |

The audited release matrix includes native debug and release suites, ASan/UBSan,
six libFuzzer targets, a real Linux ELF32 build, Emscripten and production browser
smokes, complete 4,312-output conformance, full cold/warm packaging benchmarks,
the official Godot 4.5 runtime, a big-endian PowerPC libogc2 build, and Dolphin
execution. Physical Wii hardware remains a release-depth follow-on, not an
unproven core-semantics gate.

See `docs/lib-plan-compliance.md` for the Step 0–17 and Definition of Success
mapping, exact replay commands, and current reference-contract counts.
