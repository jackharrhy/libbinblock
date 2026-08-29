# Wii / libogc2 integration

This adapter keeps GX and filesystem types out of the public core ABI. It can:

- load an endian-stable `.bbm` envelope from host-provided bytes;
- enumerate a bounded semantic program through the same C compiler used by the
  native and Wasm hosts;
- CPU-bake a selected output on demand;
- convert canonical straight-alpha RGBA8 into `GX_TF_RGBA8` 4×4 AR/GB tiles;
- hand the tiled bytes to a host upload callback, where a libogc2 consumer can
  flush the cache and initialize its `GXTexObj`.

Configure a devkitPPC build with the provided toolchain file and a concrete SDK
path:

```sh
cmake -S . -B .build/wii \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/devkitPPC.cmake \
  -DBB_BUILD_WII=ON -DBB_BUILD_TESTS=OFF -DBB_BUILD_CLI=OFF
cmake --build .build/wii
```

The host owns disc/SD/package reads, scratch memory, cache flushes, GX objects,
and display lists. The native unit suite exercises the same BBM load, bounded
enumeration, bake, tile conversion, and upload callback without requiring Wii
hardware.

## libogc2 GX demo

`demo/` is a real devkitPPC/libogc2 consumer. It loads an embedded BBM v1
package generated from `demo/suite.binscript`, verifies its bounded output,
bakes 12 images with widths from 48–80 pixels and heights from 32–72 pixels,
converts them to tiled
`GX_TF_RGBA8`, flushes the CPU cache, and initializes a `GXTexObj` for each one.
The live demo displays the complete gently bobbing 4×3 gallery at each
Binblock's native pixel dimensions with nearest-neighbor sampling. Its suite
uses the canonical hard primary, low-color, neutral, and warm skin palette,
with gradients reserved for explicitly radial or diagonal examples. HOME exits
the demo. It prints `BINBLOCK_WII_DEMO_OK` only after the complete
load/enumerate/render/upload path succeeds.

The official libogc2 development image provides a reproducible build:

```sh
docker run --rm --user "$(id -u):$(id -g)" \
  -e DEVKITPRO=/opt/devkitpro -e DEVKITPPC=/opt/devkitpro/devkitPPC \
  -e PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:/usr/bin:/bin \
  -v "$PWD:/work" -w /work ghcr.io/extremscorner/libogc2:latest sh -c '
    cmake -S . -B .build/wii-libogc2 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/devkitPPC.cmake \
      -DCMAKE_BUILD_TYPE=Release &&
    cmake --build .build/wii-libogc2 &&
    make -C integrations/wii/demo \
      BINBLOCK_BUILD=/work/.build/wii-libogc2
  '
```

The resulting `demo/binblock-wii-demo.dol` runs on Wii hardware or Dolphin. A
headless Dolphin smoke can be launched with:

```sh
/path/to/Dolphin -b \
  -e "$PWD/integrations/wii/demo/binblock-wii-demo.dol" \
  -u "$PWD/.build/dolphin-wii-user"
```

The runtime gate requires the OS report marker
`BINBLOCK_WII_DEMO_OK outputs=1 cardinality=12 textures=12 dimensions=48..80x32..72 gx-bytes=178944`.
