# ADR 0005: Backend lowering and fallback

Status: amended

## Decision

The portable core owns graph semantics and the canonical CPU renderer. Target
adapters may query immutable graph nodes and lower the subset they support, but
the core does not contain a generic backend capability or partition planner.
A shared planner should be introduced only when two production adapters need the
same partitioning behavior.

WebGL2 is a narrow JavaScript host bridge over the Wasm graph-query ABI. It
lowers supported pure nodes to fragment shaders, validates previews against the
canonical rasterizer at the declared tolerance, and selects CPU output on an
unsupported node, excessive error, shader failure, or lost context. GPU objects
never enter the C ABI.

SDL GPU is a separate optional C adapter. It lowers a finite set of graph roots
into copied brush and stop records, then renders a batch with one instanced draw.
It selects SPIR-V on native SDL GPU drivers and WGSL on SDL's WebGPU driver, so
desktop and browser presentation share the same adapter. The host owns SDL
objects, submission, device-loss recovery, and any CPU fallback.

Godot consumes the C ABI through GDExtension resources and initially bakes
`ImageTexture` values on CPU. Wii consumes BinScript source, bakes bounded
outputs, and converts them to GX tiled texture bytes before a host upload
callback. Godot objects, GX objects, files, and cache-flush mechanisms remain
host-owned.

## Consequences

- CPU semantics remain authoritative and headless operation always works.
- Acceleration can grow one operation at a time without changing the graph.
- An accelerated adapter must validate or document its equivalence; it cannot
  silently claim exactness.
- Device/context loss cannot invalidate or mutate the compiled program.
