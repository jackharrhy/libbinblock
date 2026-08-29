# ADR 0002: Image semantics and render profiles

Status: accepted

## Decision

The first canonical surface is `RGBA8_UNORM`, numeric sRGB, straight alpha, with
an explicit row pitch. Stored RGB is meaningful even when alpha is zero. The
portable software rasterizer is the conformance implementation.

Operation details that can change bytes—sample locations, interpolation,
rounding, clamping, edge behavior, alpha arithmetic, and resize kernels—belong to
a named render profile. The archive compatibility contract is documented in
[`reference-set-exact-v1.md`](../render-profiles/reference-set-exact-v1.md).
A later modern/default profile may choose different color behavior, but it must
be named and tested rather than silently replacing this profile.

Backends report an operation as exact, bounded-error with a declared tolerance,
visual-only, or unsupported. Unsupported graph regions are eligible for CPU bake
and upload. GPU output is not presumed byte-identical.

## Consequences

Public core headers do not expose Canvas, PNG, Godot, WebGPU, WebGL, or GX types.
Encoded-byte equality and decoded-pixel equality remain separate contracts.
