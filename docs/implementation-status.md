# Implementation status

The major architectural components have an implementation in this tree. The
important gates and their evidence are:

| Phase               | Implemented evidence                                                                                                                                                      |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Reference contract  | Deterministic 4,312-file manifest, family counts, hashes, aliases, smoke fixtures, and per-file equivalence TSV                                                           |
| Portable foundation | Conservative C11 CMake target, host allocator, limits, UTF-8, source spans, diagnostics, warnings-as-errors, sanitizer option                                             |
| Raster semantics    | Documented exact profile and canonical RGBA8 implementations for transforms, composition, masks, gradients/fields, assets, and Lanczos3                                   |
| Semantic graph      | Immutable typed nodes, structural hashes, provenance, validation, direct C construction, raster execution                                                                 |
| Collections         | Lazy bounded map/filter/flat-map/concat/zip/product/select/slice with checked cardinality and indexed access                                                              |
| BinScript           | Lossless syntax/recovery, semantic typing/lowering, built-ins, trace queries, host modules/assets, scalar overrides                                                       |
| CLI/conformance     | Check/list/render/package/compare commands and complete 4,312-output manifest conformance                                                                                 |
| Wasm/notebook       | Generation-checked integer handles, copy-out ABI, compact zero-asset generated-set preset, bounded previews, C-owned production notebook                                  |
| Platform rendering  | Canonical CPU rendering, target-local WebGL2 lowering, optional SDL GPU SPIR-V/WGSL batching, Godot images, and Wii GX texture conversion                                 |
| SDL GPU             | Finite graph-recipe lowering, one-draw 256-item atlas, Vulkan and experimental SDL WebGPU differential passes, and an Emscripten browser demo                             |
| Godot               | Godot 4.5+ GDExtension/runtime gate, live `BinProgram` parameters, native-sized harsh-palette editor gallery, and Binblock-powered `TileMapLayer`/`CharacterBody2D` world |
| Wii                 | devkitPPC/libogc2 build, source host, bounded enumeration, CPU bake, GX RGBA8 tiling/upload, and Dolphin-rendered native-sized harsh-palette gallery                      |

The audited release matrix includes native debug and release suites, ASan/UBSan,
five libFuzzer targets, a real Linux ELF32 build, Emscripten and production browser
smokes, complete 4,312-output conformance, full cold/warm packaging benchmarks,
the official Godot 4.5 runtime, a big-endian PowerPC libogc2 build, and Dolphin
execution. Physical Wii hardware remains a release-depth follow-on, not an
unproven core-semantics gate.
