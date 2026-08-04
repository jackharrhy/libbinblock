# Bingen roadmap

## Current state

- The embedded reference set contains 4,312 PNGs and is represented by `createReferenceSetRasterRecipe()`.
- `bin-block-recipe/v1` is validated with Zod and exports as JSON Schema.
- Mapbox-style expressions provide deterministic scalar parameters and naming.
- Stages emit ordered artifact sets; downstream stages can consume those sets through `forEach` bindings.
- Stage inputs support expression-based filtering and keyed selection in addition to Cartesian expansion.
- The compiler rejects invalid operations, references, dependency cycles, duplicate keys, duplicate paths, unsafe paths, and excessive expansion.
- The standalone browser build previews and exports all 4,312 default images.
- Executable family recipes and their JSON Schema are included in collection ZIPs.

## Family status

`Pipeline` means both atlas preview and ZIP export use `compileRecipe()` for that family.

| Semantic family         |   Images | Recipe                               | Frontend                             | Exactness                                                    | Remaining work                                                                                                                   |
| ----------------------- | -------: | ------------------------------------ | ------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------- |
| `flat-color`            |      16+ | Analytic                             | Pipeline                             | Pixel-exact                                                  | Add RGBA values and configurable dimensions                                                                                      |
| `gradient-masks`        |       19 | Analytic + one raster                | Pipeline                             | Alpha-exact `00-17`; raster-exact `18`                       | Replace `reference-set/alpha-map` with generic geometry parameters and preserve historical white RGB quantization where required |
| `gradient-variants`     |    1,202 | Executable raster recipe             | Recipe-backed atlas/byte passthrough | Raster-exact                                                 | Model palette × field × overlay × opacity × rotation; recover high-precision `gradNN` fields                                     |
| `downscaled`            |       88 | Composable                           | Pipeline                             | Pixel-exact at default                                       | Feed canonical gradient-variant artifacts instead of pinned 8×8 sources after high-precision fields are recovered                |
| `foreground-alpha`      |       22 | Two-stage composable                 | Pipeline                             | Raster-exact default; configurable tint                      | Replace pinned alpha fields with analytic or imported mask stages where possible                                                 |
| `foreground-composites` |      815 | Three-stage dependent recipe         | Pipeline                             | Raster-exact default; custom colors match recovered renderer | Replace pinned output fallbacks as the remaining one-channel differences are resolved                                            |
| `elliptical-gradients`  |       65 | Executable transformed-raster recipe | Pipeline                             | Raster-exact default                                         | Encode each organic asset as multi-stop ellipse, axial field, raster layer, or ordered composition                               |
| `layer-compositions`    |    1,061 | Executable raster recipe             | Recipe-backed atlas/byte passthrough | Raster-exact                                                 | Convert source maps, fills, crop, offset, and source-over rules into recipes; recover special `0500*` and modding `1901-1917`    |
| `sans-glyphs`           |       26 | Executable two-color recipe          | Pipeline                             | Raster-exact default                                         | Identify and pin font, face, rasterization, baseline, hinting, and no-AA behavior; then use `glyph` adapter                      |
| `serif-glyphs`          |       26 | Executable two-color recipe          | Pipeline                             | Raster-exact default                                         | Identify source font and layout; encode overlays as composition operations                                                       |
| `ordered-results`       |      972 | Alias + exception recipe             | Recipe-backed atlas/byte passthrough | 960 pixel aliases; 12 raster exceptions                      | Feed aliases from analytic gradient variants once that family is migrated                                                        |
| Custom permutations     | Variable | Four-stage analytic recipe           | Pipeline                             | Deterministic                                                | Expose its operation structure through the future recipe editor                                                                  |

## Compiler work

- Add collection union operations, including an explicit `zip` combinator.
- Add lazy planning and materialization. Raster-only outputs should pass original encoded bytes to ZIP export without decoding and re-encoding.
- Add expected pixel hashes, analytic verification, and declarative `onMismatch` raster fallback.
- Add normalized provenance to each artifact rather than storing only collection-level summaries.
- Add generic pixel correction, placement/canvas, and richer alpha-field operations.
- Make operation schemas extensible without editing the central Zod union while retaining closed validation for each registered namespace.
- Add asset import, content hashing, dimension checks, and browser/Node font adapters.
- Add collection-level recipe composition so selected family documents can be imported into one dependency graph rather than compiled independently.
- Define profile semantics for numeric sRGB, interpolation, alpha handling, resize kernels, font rasterization, and PNG encoding.

## Frontend work

- Replace the hard-coded `STAGES` catalogue with metadata derived from recipe definitions and schemas.
- Make the visual form and raw JSON editor two views of the same `bin-block-recipe/v1` document.
- Replace archive-group selection and `OUTPUT_PREFIXES` routing with recipe outputs.
- Make atlas preview materialize lazy recipe artifact plans for raster-only families instead of using the embedded reference atlas as an optimization.
- Replace `buildCollection()` orchestration with a generic lazy recipe package writer.
- Add operation-stack editing, parameter references, reusable definitions, validation locations, and output provenance inspection.
- Add undo/redo, snapshots, recipe import/export, asset management, and output-count warnings.
- Add accessible non-drag reorder controls and mobile operation editing.

## Formula recovery

- Recover exact high-precision formulas for historic `grad00-grad17` color fields.
- Eliminate sparse blue Lanczos corrections by reproducing the upstream fields precisely.
- Resolve the one-channel red foreground-composite differences.
- Recover analytic recipes for special `0500*` outputs and modding `1901-1917`.
- Replace brown-bear and eye raster recipes with editable layer stacks.
- Identify both glyph source fonts and exact rasterization settings.
- Classify the remaining duplicate groups as byte aliases, pixel aliases, or independently encoded equivalents.

## Suggested order

1. Add lazy raster passthrough, then make the full frontend compile one collection recipe.
2. Replace the hard-coded stage catalogue and collection configuration with recipe-derived metadata.
3. Add generic collection union and import/export of composed family recipes.
4. Migrate layer compositions and organic gradients from raster recipes to analytic stacks.
5. Add deterministic font assets and replace both glyph-mask recipes with `glyph` operations.
6. Replace compatibility operations as exact analytic forms are recovered.
