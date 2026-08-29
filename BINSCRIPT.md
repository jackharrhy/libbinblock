# BinScript

BinScript is the primary libbinblock interface. Its production implementation is the
portable C frontend and semantic compiler under `lib/frontend` and
`lib/semantic`; the browser, CLI, Godot adapter, and Wii adapter consume the same
compiled graph. JSON recipes are retained only in `legacy-ts/` for differential
evidence and are not the core IR.

## Current language

The current C-owned vertical slice supports:

- Host-resolved imports plus built-in `binblock/basic` and
  `binblock/reference-set` modules.
- Immutable bindings with `:=`.
- Shared numeric parameters such as `size := 64`.
- Named palettes and `map(fill)` expansion.
- CSS-inspired linear and radial gradients, including percentage stops and `lg`/`rg` shorthands.
- Fluent `size`, `mask`, `over`, `opacity`, `crop`, `canvas`, and quarter-turn
  `rotate` operations, plus `invert-alpha`, visible-RGB replacement through
  `rgb`, chroma `tint`, and two-color `remap`. Unary image methods lift over
  ordered image collections.
- Pure assignments and notebook output from standalone image-set expressions.
- Lazy ordered collections with bounded indexed slices, mapping, filtering,
  flattening, concatenation, zip, product, selection, and stable artifact keys.
- Source-ranged structured diagnostics, semantic trace queries, and typed scalar
  parameters that recompile graph shape through the Wasm adapter.
- Two-dimensional vectors and named radial-gradient geometry (`center`,
  `radius`, and compatibility `easing`) without exposing host or JSON types.
- Immutable image nodes for fills, fields, gradients, transforms, masks,
  composition, resize, and host-resolved assets.
- Precompiled `.bbm` envelopes with explicit byte order, version, and hash.
- A generated lazy reference program covering all 4,312 outputs with explicit
  analytic, alias, and raster-backed conformance contracts.

BinScript is parsed and lowered without evaluating arbitrary JavaScript. The C
semantic graph—not the old Zod schema—defines production execution.

## Language work

The following are language evolution work, not blockers for the established C
ownership boundary:

- Reusable functions with parameters, lexical bindings, and composition-friendly callbacks.
- Named operation arguments and option records without exposing recipe JSON.
- Scalar expressions: arithmetic, interpolation, conditionals, strings, path templates, and metadata access.
- Further fluent coverage for engine operations: named composite offsets,
  glyph construction, and first-class alias declarations.
- First-class output naming, directory routing, provenance, and export declarations.
- Compatibility modules for exact reference-set operations that are not analytic yet.
- Preview controls for limits, pagination, representative samples, dimensions, and output counts.
- Richer package export from a program, including texture atlases and additional
  package formats beyond the existing bounded PNG/encoded-byte passthrough path.
- Editor tooling: a structural CodeMirror language, completion, hover documentation, formatting, rename, source-mapped schema errors, and module navigation.
- Persistence: local snapshots, file import/export, shareable source, and recovery of unsaved edits.

## Reference pipeline

`reference-set/reference-set.binscript` describes the complete 4,312-image
pipeline as one lazy visible program. Known formulas are generic graph nodes;
unrecovered behavior is an explicit compatibility asset or alias. The production
notebook starts with a small editable program for fast interaction and can compile
the complete program through the same Wasm API.

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
import "binblock/basic"
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

Names and combinator details can evolve, but every compatibility dependency must
remain explicit and full materialization happens only through an export/package
request.

## Compatibility evolution

The default program already enumerates the locked archive lazily. Compatibility
assets remain explicit until recovered formulas or pinned font pipelines satisfy
their per-file contracts; they are removed incrementally, never by changing core
pixel semantics.
