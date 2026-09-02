# BinScript

BinScript is the primary libbinblock interface. Its production implementation is the
portable C frontend and semantic compiler under `lib/frontend` and
`lib/semantic`; the browser, CLI, Godot adapter, and Wii adapter consume the same
compiled graph. BinScript is the sole high-level authoring format in this
repository; the semantic graph is its core IR.

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
  flattening, concatenation, zip, product, selection, stable artifact keys, and
  generic `mask-pair`/`over-pair` mappings for artifact products.
- Source-ranged structured diagnostics, semantic trace queries, and typed scalar
  parameters that recompile graph shape through the Wasm adapter.
- Two-dimensional vectors and named radial-gradient geometry (`center`,
  `radius`, and compatibility `easing`) without exposing host or JSON types.
- Immutable image nodes for fills, fields, gradients, transforms, masks,
  composition, resize, and host-resolved assets.
- A compact, asset-free generated set for interactive use and a separate lazy
  reference program with explicit analytic, alias, and raster-backed
  conformance contracts.

BinScript is parsed and lowered without evaluating arbitrary JavaScript. The C
semantic graph defines production execution.

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

## Generated set and reference corpus

[`examples/generated-set.binscript`](examples/generated-set.binscript) is the
browser preset. It defines 16 colors and 16 analytic layers, combines them with
`product(blocks, layers).map(over-pair)`, and derives small variants through the
normal image API. Its 544 outputs are lazy, editable, and require no manifest or
raster assets:

```binscript
import "binblock/basic"
size := 64
colors := default-palette()

flat := colors.map(fill).size(size)
layers := collect([black-x, black-y, white-x, white-y])
variants := product(flat, layers).map(over-pair)
small := variants.size(8)
```

`reference-set/reference-set.binscript` has a different job: it describes the
locked archive used by CLI and CI conformance checks. Known formulas use generic
graph nodes; unrecovered behavior remains an explicit compatibility asset or
alias. It is not copied into the normal browser build. An asset can be removed
from that developer-only corpus when a native formula satisfies its declared
per-file contract.

## Compatibility evolution

The conformance program enumerates the locked archive lazily. Compatibility
assets remain explicit until recovered formulas or pinned font pipelines satisfy
their per-file contracts; they are removed incrementally, never by changing core
pixel semantics. Application programs do not inherit those compatibility assets
or their packaging cost.
