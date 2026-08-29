# Bingen follow-on work

The `LIB_PLAN.md` architecture and its completion gates are implemented and
audited in `docs/lib-plan-compliance.md`. Production semantics are owned by
portable C; the browser and CLI share the compiler and rasterizer; collections
are lazy; all 4,312 reference outputs conform; and WebGL2, Godot 4.5, libogc2,
and Dolphin runtime paths have passed their integration gates.

Remaining work is product depth and release engineering:

- Validate packaged Wii builds on physical hardware and add a direct GX/TEV
  lowering path beyond the current bounded CPU-bake and tiled-texture upload.
- Publish signed Godot extension binaries for supported editor platforms, make
  generated textures directly assignable as `Texture2D` resources, add a
  first-class Binblock atlas/`TileSet` importer, and build richer editor UX
  around the existing resource loader and Inspector properties.
- Add a WebGPU lowering adapter and broaden accelerated coverage while retaining
  explicit per-operation tolerances and CPU fallback.
- Evolve BBM beyond the version-1 endian-stable validated-source envelope into a
  compact semantic graph encoding only after that wire format is deliberately
  versioned.
- Recover more historic analytic formulas, identify and pin both font pipelines,
  and replace raster-backed compatibility assets only when their per-file gates
  pass.
- Grow persistent fuzz corpora, longer scheduled fuzz runs, and performance
  regression thresholds around the existing six libFuzzer targets and cold/warm
  benchmark suite.
- Add richer package outputs such as texture atlases, engine-native imports, and
  additional original-byte-preserving container formats.

The locked archive, TypeScript oracle, and differential tests remain evidence;
they do not define production language, collection, graph, or pixel semantics.
