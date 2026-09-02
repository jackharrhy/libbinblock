# Render profile: `reference-set-exact/v1`

Status: normative for the initial `libbinblock` CPU rasterizer.

This profile freezes the pixel-visible behavior recovered from the TypeScript
oracle and the reference archive. Generic operations belong to `binblock-raster`;
archive-only tables and corrections belong to the `binblock/reference-set`
compatibility module.

Unless an operation is explicitly marked as a compatibility exception, an
implementation claiming exact support must produce the same RGBA bytes described
here. An accelerated adapter that cannot do so must declare its tolerance or
fall back to the reference CPU renderer.

## 1. Surface and arithmetic model

An initial surface has these properties:

- width and height are positive integers;
- format is `RGBA8_UNORM` in byte order R, G, B, A;
- rows are top-to-bottom and pixels within a row are left-to-right;
- `(0, 0)` is the top-left pixel;
- alpha is straight, not premultiplied;
- RGB is numeric sRGB. No transfer to linear light occurs for interpolation,
  composition, filtering, or remapping;
- RGB beneath alpha zero is data and is preserved unless an operation below says
  it clears or rewrites it;
- a tightly packed owned surface has row pitch `width * 4`. Borrowed views may
  have a larger validated pitch;
- dimension, pitch, allocation, and pixel-count arithmetic is checked before
  allocation.

The canonical byte index is:

```text
index(x, y, channel) = y * row_pitch + x * 4 + channel
channel: R=0, G=1, B=2, A=3
```

Reference calculations use IEEE-754 binary64 intermediates. No operation may
depend on the host floating-point rounding mode.

Two conversions are used because the historical oracle used both explicit
`Math.round` and direct writes to an 8-bit clamped array:

```text
R(x) = floor(x + 0.5)                  for non-negative x
C(x) = 0                               if x <= 0 or NaN
       255                             if x >= 255
       nearest integer                 otherwise, ties to the even integer
```

`R8(x)` means `clamp(R(x), 0, 255)`. `C(x)` is ECMAScript `ToUint8Clamp`.
Operations specify which conversion they use. Integer subtraction such as
`255 - A` is exact.

## 2. Colors and transparent RGB

The semantic graph supplies colors as four bytes. BinScript hexadecimal literals
map as follows before graph construction:

```text
#RRGGBB   -> [RR, GG, BB, 255]
#RRGGBBAA -> [RR, GG, BB, AA]
```

All hexadecimal pairs are base 16 and case-insensitive. In particular,
`#ffffff00` is `[255, 255, 255, 0]`, not transparent black. Color interpolation
processes all four straight channels independently unless an operation states
otherwise.

## 3. Allocation, fill, crop, placement, and rotation

### 3.1 Clear and fill

A newly cleared surface contains `[0, 0, 0, 0]` in every pixel. Fill writes the
requested RGBA bytes unchanged to every pixel.

### 3.2 Crop

`crop(source, x, y, width, height)` requires positive output dimensions and
integer `x` and `y`. For output pixel `(ox, oy)`, let `(sx, sy) = (x + ox,
y + oy)`.

- If `(sx, sy)` is inside `source`, copy all four bytes exactly.
- Otherwise write `[0, 0, 0, 0]`.

Cropping may therefore enlarge an image or use negative origins. It never clamps
the requested rectangle to the source dimensions.

### 3.3 Canvas placement

The current `canvas(width, height, x, y)` language operation is exactly:

```text
source_over(fill(width, height, #00000000), source, x, y, opacity=1)
```

It is not a raw byte copy. Consequently a covered source pixel with alpha zero is
combined with transparent black and becomes `[0, 0, 0, 0]`.

### 3.4 Quarter-turn rotation

Normalize integer turns with `((turns % 4) + 4) % 4`. Zero turns returns a byte
copy. Two turns preserve dimensions. One or three turns exchange width and
height. For every source `(x, y)`:

```text
turns=1: destination = (height - 1 - y, x)          # clockwise
turns=2: destination = (width - 1 - x, height - 1 - y)
turns=3: destination = (y, width - 1 - x)           # counter-clockwise
```

All four bytes are copied exactly.

## 4. Per-pixel transforms

All transforms below return a new surface with the source dimensions.

### 4.1 Opacity

For finite `p` in `[0, 1]`:

```text
RGB' = RGB
A'   = R8(A * p)
```

### 4.2 Alpha inversion

```text
RGB' = RGB
A'   = 255 - A
```

### 4.3 Visible-RGB replacement

Given target RGB `T`:

```text
if A != 0: RGB' = T
else:      RGB' = RGB
A' = A
```

This operation deliberately retains the original RGB of fully transparent
pixels.

### 4.4 Chroma tint

For every pixel, including transparent pixels:

```text
m = min(R, G, B)
c = max(R, G, B) - m
R' = C(m + c * target.R / 255)
G' = C(m + c * target.G / 255)
B' = C(m + c * target.B / 255)
A' = A
```

### 4.5 Two-color remap

Let source foreground `SF`, source background `SB`, target foreground `TF`, and
target background `TB` be RGB vectors. Define:

```text
d = dot(SF - SB, SF - SB)
t = clamp(dot(RGB - SB, SF - SB) / d, 0, 1)
RGB'channel = C(TBchannel * (1 - t) + TFchannel * t)
A' = A
```

`d == 0` is an error. The projection and interpolation are numeric sRGB.

### 4.6 RGB base shift

For source base `S` and target base `T`:

```text
RGB'channel = C(Tchannel + RGBchannel - Schannel)
A' = A
```

## 5. Masking

Source and mask dimensions must match. Mask RGB is ignored and source RGB is
preserved.

```text
mode=replace:  A' = mask.A
mode=multiply: A' = R8(source.A * mask.A / 255)
RGB' = source.RGB
```

## 6. Straight-alpha source-over

The destination dimensions define the output. Start with an exact byte copy of
the destination. Integer offsets place the source origin; source pixels outside
the destination are skipped.

For each covered pixel and finite global opacity `p` in `[0, 1]`:

```text
sa = source.A / 255 * p
da = destination.A / 255
oa = sa + da * (1 - sa)
```

If `oa == 0`, write `[0, 0, 0, 0]`. Otherwise:

```text
out.RGBchannel = R8(
  (source.RGBchannel * sa + destination.RGBchannel * da * (1 - sa)) / oa
)
out.A = R8(oa * 255)
```

This is straight-alpha source-over in numeric sRGB. A source pixel with zero
alpha covering a destination pixel with zero alpha clears any invisible RGB in
that destination pixel because `oa == 0`. Destination pixels not visited because
of clipping retain all original bytes.

## 7. Easing

Inputs to easing are clamped to `[0, 1]` before evaluation.

```text
linear(t)     = t
smoothstep(t) = t*t*(3 - 2*t)
reference(t)     = 0.5*t + 0.5*(3*t*t - 2*t*t*t)
```

The compatibility-only radial correction set is:

```text
{98, 116, 234, 433, 601, 720, 922}
```

It contains unnormalized integer squared distances from a declared center.

## 8. Alpha fields

Fields evaluate at integer pixel coordinates `(x, y)`. Coordinates refer to
pixel centers; there is no additional `0.5` offset.

For center `(cx, cy)`:

```text
dx = x - cx
dy = y - cy

metric=x:         distance = dx
metric=y:         distance = dy
metric=euclidean: distance = sqrt(dx*dx + dy*dy)
metric=chebyshev: distance = max(abs(dx), abs(dy))
metric=border:    distance = min(x, y, width - 1 - x, height - 1 - y)
```

For non-border fields, radius must be finite and non-zero:

```text
t = easing(clamp(distance / radius, 0, 1))
alpha = R8(255 * (direction == in ? 1 - t : t))
```

If `referenceRadialRounding` is enabled for a Euclidean field and `dx*dx +
dy*dy` is in the correction set, add `+1` for direction `in` or `-1` for
direction `out`, then clamp on storage.

For a border field with `levels`, use:

```text
i = min(level_count - 1, max(0, floor(distance)))
alpha = levels[i]
```

Finally, with field color `[R, G, B, CA]`, write:

```text
out = [R, G, B, R8(alpha * CA / 255)]
```

Thus field color RGB is retained even where the resulting alpha is zero.

## 9. Preset gradients

Preset gradients use endpoint-normalized coordinates:

```text
nx = width  == 1 ? 0.5 : x / (width  - 1)
ny = height == 1 ? 0.5 : y / (height - 1)
```

Before field evaluation, normalized quarter-turns transform `(nx, ny)` as:

```text
1: (1 - ny, nx)
2: (1 - nx, 1 - ny)
3: (ny, 1 - nx)
```

The original preset functions are:

```text
top-down:    1 - ny
bottom-up:   ny
left-right:  1 - nx
right-left:  nx
radial-in:   max(0, 1 - hypot(nx - 0.5, ny - 0.5) / sqrt(0.5))
radial-out:  min(1, hypot(nx - 0.5, ny - 0.5) / sqrt(0.5))
diagonal:    1 - (nx + ny) / 2
corner:      1 - max(nx, ny)
```

Clamp the result to `[0, 1]`; alpha is `R8(result * 255)`. The semantic gradient
color supplies RGB unchanged at every pixel.

## 10. Linear gradients

Angles follow CSS image-gradient orientation in a y-down image: `0` degrees
points up, `90` right, and `180` down.

```text
radians = degrees * pi / 180
directionX = sin(radians)
directionY = -cos(radians)
```

If a direction component is within `1e-12` of zero, use exact zero. If its
absolute value is within `1e-12` of one, use exact `sign(value)`.

```text
halfProjection = (
  abs(directionX) * max(1, width - 1) +
  abs(directionY) * max(1, height - 1)
) / 2

automaticExtent = halfProjection == 0 ? 1 : halfProjection
length = explicitExtent if present, otherwise 2 * automaticExtent
```

`length` must be positive. For each pixel:

```text
cx = width  == 1 ? 0 : x - (width  - 1) / 2
cy = height == 1 ? 0 : y - (height - 1) / 2
position = clamp((cx*directionX + cy*directionY + automaticExtent) / length, 0, 1)
```

Note that an explicit extent replaces `length`; it does not replace the
`automaticExtent` offset in the numerator. This preserves the oracle behavior.

## 11. Elliptical gradients

The default center is `((width - 1)/2, (height - 1)/2)`. Radii must be positive.
Rotation is in radians at the semantic-node level. With `c = cos(rotation)` and
`s = sin(rotation)`:

```text
dx = x - centerX
dy = y - centerY
rx =  dx*c + dy*s
ry = -dx*s + dy*c
distance = clamp(hypot(rx/radiusX, ry/radiusY), 0, 1)
```

The distance is used as the stop position. If `referenceRadialRounding` is enabled,
the unrotated squared distance `dx*dx + dy*dy` is checked against the correction
set after interpolation. Add `+1` to alpha when the first stop alpha is greater
than the last stop alpha; otherwise add `-1`, then clamp on storage.

## 12. Stop selection and interpolation

Linear and elliptical gradients share this rule:

1. Stably sort stops by ascending offset.
2. Offsets must lie in `[0, 1]`; at least two stops are required.
3. Select the first stop whose offset is greater than or equal to the position as
   `right`. If none exists, use the last stop.
4. `left` is the preceding stop, or the first stop when `right` is first.
5. `raw = 0` when both offsets are equal; otherwise
   `(position - left.offset) / (right.offset - left.offset)`.
6. Clamp `raw`, then apply `right.easing`, else `left.easing`, else the gradient's
   default easing.
7. For each straight RGBA channel, write
   `R8(left * (1 - amount) + right * amount)`.

Equal-offset ordering is therefore observable and must remain stable.

## 13. Reference alpha maps `00` through `18`

Maps `00` through `17` are compatibility recipes over generic alpha fields. Map
`18` is not a generic operation: it is a required 64x64 raster asset until its
formula is recovered.

The map tuple is `(width, height, offsetX, offsetY, geometry, RGB)`:

| Maps      | Tuple(s)                                                                                                                                                                                                      |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `00`-`08` | `(64,64,0,0,0,black)`, `(64,64,0,0,1,black)`, `(64,64,0,0,2,black)`, `(63,64,1,0,3,black)`, `(63,63,1,1,4,black)`, `(64,64,0,0,5,black)`, `(63,63,1,1,6,black)`, `(64,64,0,0,7,black)`, `(62,62,1,1,8,black)` |
| `09`-`17` | `(64,64,0,0,0,white)`, `(64,63,0,1,1,white)`, `(64,64,0,0,2,white)`, `(63,64,1,0,3,white)`, `(63,63,1,1,4,white)`, `(64,64,0,0,5,white)`, `(64,64,0,0,7,white)`, `(63,63,1,1,6,white)`, `(62,62,1,1,8,white)` |

Geometry formulas use local integer coordinates:

```text
axis(p) = R8(255 * (1 - reference(p / 64)))

0: axis(y)
1: axis(63 - y)
2: axis(x)
3: axis(63 - x)
4: R8(255 * (1 - reference(sqrt((x-31)^2 + (y-31)^2) / 32)))
5: R8(255 *      reference(sqrt((x-32)^2 + (y-32)^2) / 32))
6: R8(255 * (1 - reference(max(abs(x-31), abs(y-31)) / 32)))
7: R8(255 *      reference(max(abs(x-32), abs(y-32)) / 32))
8: [53,101,146,179,206,255][min(5, min(x,y,61-x,61-y))]
```

For geometries 4 and 5, apply the radial correction set: `+1` for geometry 4 and
`-1` for geometry 5. RGB is `[0,0,0]` for maps `00`-`08` and `[255,255,255]` for
maps `09`-`17`, including fully transparent pixels.

The offsets are metadata used when composing a native-size map onto a 64x64
destination. They do not change field coordinates.

## 14. Lanczos3 resize

Resize is separable and uses clipped, renormalized Lanczos3 contributions. For
one source axis size `S` and destination size `D`:

```text
scale       = S / D
filterScale = max(1, scale)
support     = 3 * filterScale
center(d)   = (d + 0.5) * scale - 0.5

sinc(v)    = 1                         if v == 0
             sin(pi*v) / (pi*v)        otherwise
lanczos(v) = sinc(v) * sinc(v/3)       if abs(v) < 3
             0                         otherwise
```

For destination coordinate `d`, visit integer source coordinates from
`ceil(center-support)` through `floor(center+support)`, inclusive and ascending.
Compute `lanczos((center-source)/filterScale)`, discard zero weights, discard
out-of-bounds samples, then divide every remaining weight by the sum of retained
weights. Edges are clipped and renormalized; they are not extended, mirrored, or
wrapped.

The horizontal pass stores four binary64 values per intermediate pixel:

```text
H.R += source.R * (source.A / 255) * weight
H.G += source.G * (source.A / 255) * weight
H.B += source.B * (source.A / 255) * weight
H.A += source.A * weight
```

The vertical pass applies its weights to each `H` channel. Let the four sums be
`P.R`, `P.G`, `P.B`, and `P.A`:

```text
alpha = clamp(P.A, 0, 255)
out.A = R8(alpha)

if alpha > 0:
  out.R = R8(P.R * 255 / alpha)
  out.G = R8(P.G * 255 / alpha)
  out.B = R8(P.B * 255 / alpha)
else:
  out.RGB = [0,0,0]
```

The exact CPU implementation must use a pinned binary64 sine implementation,
precomputed contribution tables, or another tested method that yields these
final bytes; it must not assume arbitrary host `libm` implementations agree in
their last bits.

The generic resize ends here. Sparse byte corrections used by 8x8 historic blue
fixtures are keyed compatibility metadata in `binblock/reference-set`, applied
after resize and any requested quarter-turn. They are not part of generic
Lanczos3.

## 15. Decoded rasters and passthrough

A decoded raster input is validated for dimensions, format, row pitch, and byte
extent before use. Importing it copies or retains it according to the explicit
resolver contract; it never silently changes alpha mode or transparent RGB.

An output may retain encoded-asset identity when its graph performs no decoded
pixel operation. A host requesting encoded output may then return the original
bytes exactly. Any pixel operation, including a no-op-looking source-over or
zero-turn copy, ends encoded-byte identity unless the graph records an explicit
byte alias.

## 16. Equivalence and fixture contract

- Flat colors are RGBA pixel-exact.
- Reference maps `00`-`17` are alpha-exact with their documented dimensions and
  offsets. Their historic RGB quantization is not generalized beyond the exact
  black/white rules above.
- Map `18` is raster-fallback.
- Generic Lanczos3 is judged by its defined output. Historic 8x8 archive parity
  additionally permits explicit compatibility corrections.
- Comparisons include all four bytes, including RGB where alpha is zero.
- A mismatch report records dimensions, maximum channel error, differing channel
  count, first mismatch coordinate/channel, decoded RGBA8 hash, and—only for a
  byte-identity contract—the encoded-byte hash.

The hand-curated smoke set is `tests/fixtures/reference-smoke.json`; the complete
archive inventory and declared per-file equivalence classes are in
`reference-set/reference-manifest.json`.
