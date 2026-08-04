# Composable recipe model

`bin-block-recipe/v1` is the executable, validated description of a collection. It is separate from UI state and export manifests.

The recipe compiler has four layers:

1. Zod validates the closed document and operation schemas.
2. Mapbox-style expressions resolve scalar parameters and names.
3. Stages expand values and upstream artifact sets in dependency order.
4. Image operations produce deterministic RGBA buffers.

## Expressions

Expressions are JSON arrays whose first item is an allowlisted operator. Ordinary arrays must be wrapped in `literal`, following the Mapbox style specification's distinction between expressions and array data.

```json
["get", "color", ["var", "palette-entry"]]
["concat", "flat-color/", ["get", "id", ["var", "palette-entry"]], ".png"]
["literal", [64, 64]]
```

The initial operators are `literal`, `var`, `get`, `let`, `case`, `match`, `coalesce`, `concat`, `to-string`, `pad`, comparisons, arithmetic, `min`, `max`, and `clamp`. Expressions cannot load files, execute JavaScript, recurse, or manipulate images.

## Stage sets

Every stage emits an ordered set of artifacts. A later stage can consume an entire upstream set as a `forEach` axis. Multiple axes create a Cartesian product in declaration order.

```json
{
  "type": "render",
  "id": "downscaled",
  "forEach": {
    "source": { "source": "stage", "stage": "base-colors" },
    "size": { "source": "values", "values": [8, 16, 32] }
  },
  "key": ["concat", ["get", "key", ["var", "source"]], "-", ["to-string", ["var", "size"]]],
  "path": ["concat", "downscaled/", ["get", "key", ["var", "source"]], "-", ["to-string", ["var", "size"]], ".png"],
  "image": {
    "op": "resize",
    "source": { "op": "input", "binding": "source" },
    "width": ["var", "size"],
    "height": ["var", "size"]
  }
}
```

Artifact metadata is available to expressions as `{ stage, key, path, properties }`. Pixel data is only accessible through an explicit `input` image node. This keeps parameter evaluation and image dependencies separate.

Stages may appear before their dependencies in JSON. Compilation performs a stable topological sort and rejects missing stages, duplicate IDs, cycles, duplicate keys, duplicate output paths, unsafe paths, and excessive expansion.

## Image operations

The initial generic operation set is:

- Sources: `fill`, `gradient`, `raster`, `glyph`, `input`, and reusable `use` definitions.
- Composition: `composite` and `apply-mask`.
- Color and alpha: `opacity` and `set-visible-rgb`.
- Geometry: `crop`, quarter-turn `rotate`, and numeric-sRGB `lanczos3` resize.

Raster and glyph operations use injected adapters. The compiler does not depend on the DOM, Canvas, Node filesystem APIs, or the embedded default set.

Reusable definitions accept scalar parameters:

```json
{
  "definitions": {
    "colored-block": {
      "parameters": { "color": "#000000" },
      "image": {
        "op": "fill",
        "width": 64,
        "height": 64,
        "color": ["var", "color"]
      }
    }
  }
}
```

## Default set

`createDefaultSetRasterRecipe()` maps all 4,312 embedded files into a `default-set-exact/v1` recipe. Each family has its own stage, and every source file is pinned as a raster asset.

| Stage | Initial source | Analytic replacement |
| --- | --- | --- |
| `flat-color` | Raster baseline | `fill` over palette values |
| `gradient-masks` | Maps `00-18` | Generic gradients, legacy maps, raster map `18` |
| `gradient-variants` | Historic raster fields | Base set x mask set x overlay set x rotation |
| `downscaled` | Raster baseline | Upstream variants, rotation, and Lanczos3 resize |
| `foreground-alpha` | Raster baseline | Gradient/mask followed by `set-visible-rgb` |
| `foreground-composites` | Raster baseline | Base set x foreground-alpha set, then `composite` |
| `elliptical-gradients` | Organic raster baseline | Multi-stop ellipse definitions and compositions |
| `layer-compositions` | Out4 raster baseline | Ordered fills, masks, crops, offsets, and composites |
| `sans-glyphs` | Glyph-mask raster baseline | Font asset x glyph values, then glyph renderer |
| `serif-glyphs` | Glyph-mask raster baseline | Font asset x glyph values, then glyph renderer |
| `ordered-results` | Raster baseline | Alias stages targeting canonical variant artifacts |

The default profile provides a raster-backed stage for every family. Family IDs and output paths are the stable interface for downstream stages. Set-specific operations use the `default-set/` namespace.

### Current family implementations

The browser preview and ZIP exporter use the same recipe executor for these families:

- `flat-color` expands the selected and custom palette values into `fill` operations.
- `gradient-masks` expands indices `00-18` through a namespaced alpha compatibility operation. Map `18` remains a pinned raster asset.
- `downscaled` expands the 88 pinned source artifacts through `tint-chroma` and `resize` operations. The default blue 8×8 recipe remains pixel-exact for all fixtures; color and size are ordinary recipe values.
- `foreground-alpha` emits 22 pinned alpha fields, then feeds them into a configurable tint stage.
- `foreground-composites` filters the foreground-field set by key for each of 815 output recipes, preserving exact raster outputs by default and using generic fill/composite operations for custom foreground colors.
- `elliptical-gradients`, `sans-glyphs`, and `serif-glyphs` use generic transformed-raster operations while their original analytic fields and fonts remain unidentified.
- `gradient-variants` and `layer-compositions` are complete validated raster-backed recipes with original-byte export passthrough.
- `ordered-results` contains 960 first-class pixel aliases and 12 explicit raster exceptions.
- Custom permutations are a four-stage analytic recipe containing flat sources, masks, variants, and recipe aliases.

`src/app.js` contains no family-specific pixel renderers. Unresolved families use raster-backed stages or namespaced compatibility operations.

Collection ZIPs include the effective family documents under `recipes/`, plus `recipes/bin-block-recipe-v1.schema.json`. The manifest identifies each recipe and render profile. The documents can be inspected and validated independently; compiling raster-backed recipes also requires the corresponding source assets and a raster resolver.

`bin-block-collection/v3` manifests record the recipe format, effective recipe documents, total image count, materialized paths, original-byte passthrough paths, pixel-alias totals, and raster exceptions. Recipes and artifacts carry provenance directly instead of using parallel `regenerated`, `legacy`, and `fallback` classifications.

## Profiles and provenance

The format and render profile are versioned separately:

```json
{
  "format": "bin-block-recipe/v1",
  "profile": "default-set-exact/v1"
}
```

Generic recipes use `numeric-srgb/v1`. The default set uses pinned rasters for outputs that do not yet have exact analytic recipes. Alias artifacts record byte, pixel, or recipe identity. Compiled artifacts include their stage, key, properties, image, and provenance.
