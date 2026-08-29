# Reference manifest

`reference-manifest.json` is the locked inventory for all 4,312 PNG files in
`reference-set/`. Regenerate it with `npm run inventory` and verify it without
writing files with `npm run inventory:check`.

The generator uses a purpose-built decoder for the archive's non-interlaced
8-bit RGB and RGBA PNGs. It hashes canonical straight-alpha RGBA8 bytes directly,
including stored RGB in pixels whose alpha is zero; browser Canvas is not part of
the manifest contract.

Each file records its normalized path, semantic family, encoded byte size and
SHA-256, dimensions, stored pixel format, alpha presence, decoded RGBA8 SHA-256,
and current equivalence class:

- `pixel-exact`: the recovered analytic operation currently matches every RGBA
  byte.
- `alpha-only-exact`: the recovered operation matches the alpha contract while
  historic visible RGB quantization is not yet exact.
- `bounded-difference`: an analytic path exists with a measured non-zero error.
- `raster-fallback`: the encoded or decoded fixture remains the honest source of
  exact behavior.
- `pixel-alias`: the output has a named decoded-pixel-identical target.
- `byte-alias`: the complete encoded PNG byte stream has a canonical identical
  target.

`reference-set/reference-conformance.tsv` is the executable, ordered contract
used by the native comparator. In addition to the equivalence class it records
an explicit maximum per-channel error and the current implementation provenance
(`analytic`, `analytic-from-asset`, `raster-asset`, `pixel-alias`, or
`byte-alias`). Exact and alias contracts have a zero tolerance. The recovered
blue `grad00`-`grad04` analytic slice has a measured tolerance of 4 for 25%
composites, 7 for 50% composites, and 13 for full-strength and white composites.
The 80 recovered downscales use canonical 64×64 asset graph nodes followed by
the generic Lanczos3 resize and have bounds of 1 or 2. The comparator still
reports all differing units and the observed maximum even when they are inside
that bound.

`tests/fixtures/reference-smoke.json` pins a hand-curated 16-image subset covering
every archive family, transparent-RGB behavior, map 18, one ordered pixel alias,
and one ordered raster exception. It is intended for fast cross-implementation
checks before running the complete archive conformance suite.

The 960 ordered-result aliases use the recovered semantic mapping in the legacy
oracle and are rechecked against decoded pixel hashes on every inventory run. The
12 missing `grad06blk50` exports remain explicit raster fallbacks. Other duplicate
groups use the first normalized path in manifest order as their deterministic
canonical target.
