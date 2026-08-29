# ADR 0003: Graph identity, provenance, and parameters

Status: accepted

## Decision

Image nodes are immutable typed records. A graph assigns stable within-graph node
handles and deduplicates structurally equal nodes. The canonical 128-bit
structural hash includes node kind, dimensions, semantic options, child hashes,
and eventually asset content identities. It never includes host pointers,
allocator addresses, source spans, or display names.

Source provenance is an append-only attachment indexed by node handle and is not
part of semantic identity. Adding a span therefore cannot invalidate a render
cache key.

Values that change graph shape or collection cardinality are compile-time
constants. Values that may update without recompilation will be explicit render
parameters/uniforms. Images and palettes are resource parameters identified by
content identity. Volatile host inputs such as time are legal only in a future
restricted field/material graph, not in the static image graph.

## Consequences

Static graph cache keys are deterministic across equivalent graph construction.
Dynamic material work cannot smuggle mutable host values into static node
identity. Serialization remains deferred until handles and parameter tables have
settled.
