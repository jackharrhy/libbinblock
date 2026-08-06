# BinScript direction

BinScript is the primary Bingen interface. The browser opens directly into one editable program; the typed `bin-block-recipe/v1` document remains the validated compiler IR rather than an authoring format.

## Current language

The first functional slice supports:

- Path-string preambles with `import "bingen/basic"`.
- Immutable bindings with `:=`.
- Shared numeric parameters such as `size := 64`.
- Named palettes and `map(fill)` expansion.
- CSS-inspired linear and radial gradients, including percentage stops and `lg`/`rg` shorthands.
- Fluent `size`, `mask`, and `preview` operations.
- Pure assignments and notebook output from standalone image-set expressions.
- Ordered arrays of image sets; standalone flat image arrays share one preview row, while nested artifact collections retain separate rows inside one frame without unioning or compositing their artifacts.
- Source-linked sliders, color controls, diagnostics, and inline artifact previews.
- Reusable RGBA color bindings with integrated alpha-aware color pickers.
- Recursive collection materialization, `collect`, `union`, `product`, `select`, and `slice`.
- Collection-wide resize, rotate, opacity, tint, crop, mask, and source-over operations.
- Composable linear, radial, square-distance, and border-distance gradients with legacy easing/rounding, canvas placement, and visible source-over composition.
- Explicit `fixtures()` and `compare()` values with generated, reference, amplified-difference, and error-metric notebook previews.
- Visible analytic `grad00`-`grad04` fields and the first 16 historic blue-high overlay variants, with ordered fixture comparison.

BinScript is parsed and lowered without evaluating arbitrary JavaScript. Compilation still passes through the existing Zod recipe schema, dependency graph, and deterministic image engine.

## Language work

The language needs these features before it can replace every existing orchestration path:

- Collection combinators: `stack`/`union`, `zip`, Cartesian product, `map`, `flat-map`, `filter`, keyed lookup, and slicing.
- Reusable functions with parameters, lexical bindings, and composition-friendly callbacks.
- Named operation arguments and option records without exposing recipe JSON.
- Scalar expressions: arithmetic, interpolation, conditionals, strings, path templates, and metadata access.
- Full fluent coverage for engine operations: raster sources, resize, crop, rotate, opacity, tinting, two-color remapping, compositing, offsets, glyphs, and aliases.
- First-class output naming, directory routing, provenance, and export declarations.
- Import resolution for standard modules, user modules, raster assets, fonts, palettes, and saved programs.
- Compatibility modules for exact reference-set operations that are not analytic yet.
- Lazy planning so large collections can be inspected without eagerly decoding or rendering every artifact.
- Preview controls for limits, pagination, representative samples, dimensions, and output counts.
- Package export from a program, including PNG materialization, original-byte passthrough, manifests, recipes, and texture atlases.
- Editor tooling: a structural CodeMirror language, completion, hover documentation, formatting, rename, source-mapped schema errors, and module navigation.
- Persistence: local snapshots, file import/export, shareable source, and recovery of unsaved edits.

## Default pipeline

The default program must eventually describe the complete 4,312-image pipeline in visible BinScript source. Opening Bingen should show that program, not a smaller demo and not a hidden form configuration.

The document should visibly bind and compose:

1. The default palette and flat-color family.
2. Gradient masks as visible axial, Euclidean, Chebyshev, border-distance, and layered radial fields.
3. Historic gradient variants.
4. Downscaled variants.
5. Foreground-alpha fields and foreground composites.
6. Organic and elliptical gradients.
7. Layer compositions and special outputs.
8. Sans-serif and serif glyph families.
9. Ordered-result aliases and raster exceptions.
10. The final collection, atlas, manifest, and package outputs.

A target shape is:

```binscript
import "bingen/basic"
size := 64
colors := default-palette()

flat := colors.map(fill).size(size)
masks := collect([mask-00, mask-01, /* visible field bindings */, mask-18])
variants := product(colors, masks).map(render-variant)
downscaled := variants.map(resize(8))

foregrounds := reference-foregrounds(colors)
organic := reference-organic-gradients(size)
layers := reference-layer-compositions()
glyphs := reference-glyphs(colors)
results := reference-result-aliases(variants)

default := union(flat, masks, variants, downscaled, foregrounds, organic, layers, glyphs, results)
default
```

Names and combinator details will evolve, but the architectural requirement is fixed: every default family is represented by readable source, every compatibility dependency is explicit through imports, and the final standalone expression is the complete collection.

The notebook should compile the whole graph lazily. Inline previews render bounded samples at standalone expressions; only explicit export actions materialize all 4,312 outputs.

## Migration sequence

1. Add lazy collection plans and `union`/`product`/`zip` combinators.
2. Expose every existing generic image operation through BinScript.
3. Add reference-set data imports only for genuinely irreducible assets, not hidden image recipes.
4. Translate each default family into visible bindings in the starter program.
5. Add program-driven atlas and package exports.
6. Replace compatibility imports incrementally as analytic formulas and fonts are recovered.
