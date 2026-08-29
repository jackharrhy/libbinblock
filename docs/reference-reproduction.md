# Reproduction model

The project treats every output as a recipe. A recipe may be analytic, an alias, or raster-backed.

## Analytic recipes

- Flat RGB/RGBA fills.
- Axial, circular, square/Chebyshev, border-ramp, and elliptical gradients.
- Arbitrary multi-stop colors and alpha.
- Compatibility easing that combines linear and smoothstep curves.
- Native crop dimensions and placement offsets.
- Ordered source-over layers.
- Exact quarter-turn rotations.
- Lanczos3 downsampling in numeric sRGB.

## Alias recipes

The manifest records exact duplicates and pixel aliases instead of treating every historical filename as a unique image. Of the 972 numbered `result` files, 960 are pixel aliases. The out4 folders also contain substantial overlap.

## Raster recipes

Raster sources cover images whose formulas are not yet known, including map `18`, high-precision `gradNN` fields, hand-tuned gradients, and glyph masks. The shared pipeline can recolor, composite, rotate, resize, and pack these sources.

The default recipe covers every image. Raster sources can be replaced with analytic operations as their parameters are recovered, without changing output paths.
