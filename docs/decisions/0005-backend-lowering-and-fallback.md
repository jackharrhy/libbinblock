# ADR 0005: Backend lowering and fallback

## Decision

Backends publish node-kind capabilities, equivalence class, limits, upload and
readback support, and alignment preferences. The core planner partitions an
immutable graph into direct regions, CPU-bake/upload steps, or a CPU-final step.
Fallback can be disabled or bounded by total baked bytes.

WebGL2 is a narrow JavaScript host bridge over the Wasm graph-query ABI. It
lowers supported pure nodes to fragment shaders, validates previews against the
canonical rasterizer at the declared tolerance, and selects CPU output on an
unsupported node, excessive error, shader failure, or lost context. GPU objects
never enter the C ABI.

Godot consumes the C ABI through GDExtension resources and initially bakes
`ImageTexture` values on CPU. Wii consumes BBM bytes, bakes bounded outputs, and
converts them to GX tiled texture bytes before a host upload callback. Godot
objects, GX objects, files, and cache-flush mechanisms remain host-owned.

## Consequences

- CPU semantics remain authoritative and headless operation always works.
- Acceleration can grow one operation at a time without changing the graph.
- A backend must state bounded or visual equivalence; it cannot silently claim
  exactness.
- Device/context loss cannot invalidate or mutate the compiled program.
