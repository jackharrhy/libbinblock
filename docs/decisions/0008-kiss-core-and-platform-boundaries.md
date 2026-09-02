# ADR 0008: KISS core and platform boundaries

Status: accepted

## Context

The first implementation proved native, browser, Godot, and Wii execution, but
also accumulated infrastructure without production callers. The project does
not currently promise ABI or serialized-format compatibility, so this is the
right point to reduce the design to the mechanisms required by real consumers.

Relevant prior art reinforces a small pull-based core:

- [libvips](https://github.com/libvips/libvips/wiki/Why-is-libvips-quick)
  evaluates image pipelines on demand instead of eagerly materializing every
  result.
- [SVG filter primitives](https://www.w3.org/TR/SVG11/intro.html) model image
  operations as explicit nodes in a filter graph.
- [Emscripten modularized output](https://emscripten.org/docs/compiling/Modularized-Output.html)
  supports an isolated Wasm module behind a narrow exported C interface.

## Decision

The library has one target-neutral semantic model:

1. BinScript parses and lowers to immutable image nodes and finite lazy
   collection plans.
2. Hosts request output metadata or a bounded item by index.
3. The canonical renderer produces deterministic straight-alpha RGBA8.
4. Native, browser, Godot, and Wii code own I/O, presentation objects, GPU/GX
   resources, and caches.

The core keeps checked arithmetic, explicit resource limits, host allocation,
source diagnostics, graph nodes, lazy collections, and raster semantics. These
protect embedders and untrusted browser input and are not optional scaffolding.

The core does not keep unused arenas, string interners, generic backend planners,
or platform capability profiles. A new abstraction needs either two production
consumers or a measured correctness/performance requirement.

Semantic implementation files are divided by responsibility. Sharing state
through one private header is preferred to a public compiler-pass framework.

Any future deployment format will serialize the target-neutral program rather
than wrap source text. It will use explicit-width fields, bounds-checked offsets,
and byte-defined encoding suitable for big-endian PowerPC. The format should be
implemented only after the in-memory program/sequence model has a
serialization-friendly representation; it must not encode callback pointers or
platform objects.

## Consequences

- The C frontend remains in authoring builds but can eventually be omitted from
  deployment-only Wii builds.
- Target-specific acceleration stays local until shared behavior is proven.
- Raster compatibility assets remain data and are hydrated only when traversed.
- Public API growth follows concrete embedding use cases rather than exposing
  every internal subsystem preemptively.
