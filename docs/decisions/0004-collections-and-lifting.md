# ADR 0004: Collections and image-operation lifting

Status: accepted

## Decision

Collections are finite, ordered, lazy plans with checked 64-bit cardinality and
zero-based indexed access. Unknown or infinite cardinality is not accepted in the
initial runtime. `map`, `filter`, `flat_map`, concatenation, keyed selection, and
slice preserve source order. A key may select multiple artifacts, also in source
order. Duplicate keys are legal; output packaging policy may reject them later.

`zip(a, b)` requires equal cardinality and produces `(a[0], b[0])`, then
`(a[1], b[1])`, and so on. A length mismatch is an error. `product(a, b)` is
left-major: `(a[0], b[0..])`, then `(a[1], b[0..])`. Cartesian expansion occurs
only through explicit `product` or a standard function whose signature states
that behavior.

Unary image methods broadcast over a collection while preserving its order and
metadata. Binary image methods, including `mask`, use these rules:

- image with image produces one image;
- collection with image or image with collection broadcasts the scalar image;
- collection with collection uses equal-length `zip` and rejects a mismatch;
- collection with collection never implies a Cartesian product.

Cardinality over the configured maximum, or arithmetic that would exceed
`uint64_t`, returns `BB_STATUS_LIMIT_EXCEEDED` before proportional allocation.
`max_output_count` bounds render-on-demand indices. Slice is the bounded preview
primitive. Host render callbacks may implement bounded caches before delegating
to a target adapter; core collection plans own no implicit cache.

Artifacts carry stable key/path views, provenance, and an explicit alias class:
recipe identity, decoded-pixel identity, or encoded-byte identity. These classes
are not interchangeable.

## Consequences

Counting and indexing a product do not materialize it. Filtering and flat-map
counting may evaluate their source because those operations inherently need it,
but still do not allocate output-sized arrays. The compiler and direct-C API share
the same ordering contract.
