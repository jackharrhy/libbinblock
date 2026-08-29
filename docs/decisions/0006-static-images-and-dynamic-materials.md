# ADR 0006: Static images and dynamic materials

Status: accepted

## Decision

The current semantic graph describes deterministic, finite static images. It has
no implicit pixel coordinate, UV, clock, frame, or mutable host value. Compile
parameters may rebuild graph shape; a host may cache the resulting immutable
program and rendered artifacts by structural identity.

A future dynamic material feature will be a separate restricted, pure field
subset. Its only varying inputs will be explicitly declared coordinates, time,
and uniforms. A backend must advertise the field node kinds and limits it can
execute dynamically. Static image nodes do not become shaders merely because a
GPU backend can accelerate their rasterization.

The render planner may execute supported regions directly, CPU-bake unsupported
regions into textures, or reject the request when fallback is disabled. WebGL2,
Godot, and GX integrations consume the same semantic nodes and capability model;
none may add target-only meaning to a core operation.

## Consequences

The reference program, native generator, and texture integrations remain useful
without a shader language. Live uniforms and volatile values cannot silently
invalidate static hashes. A future field graph can define CPU, shader, and GX
lowering deliberately while retaining safe texture baking for unsupported
targets.
