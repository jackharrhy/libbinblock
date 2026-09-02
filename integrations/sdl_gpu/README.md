# SDL3 GPU backend

This optional adapter renders a supported BinScript image graph through
`SDL_GPU`. The same code uses Vulkan on current desktop SDL releases and WGSL
through SDL's experimental WebGPU backend. It does not replace the canonical
software renderer or add SDL to the portable core.

## How it works

`lower.c` walks each requested image root and copies the finite render recipe
into packed GPU data. Solid fills, preset gradients, linear and elliptical
gradients, alpha fields, opacity, fill resizing, and one source-over layer are
currently lowered. The generated set fits that subset: its 256 variants become
one item buffer, one gradient-stop buffer, and one instanced draw.

`pipeline.c` selects embedded SPIR-V for SDL's native GPU drivers and embedded
WGSL when the selected driver is `webgpu`. The vertex shader places each item in
the target atlas. The fragment shader evaluates its brushes and performs the
same straight-alpha RGBA8 quantization as the CPU renderer.

`binblock_sdl_gpu.c` owns the SDL buffers and records their first upload plus the
draw. The host still owns the `SDL_GPUDevice`, command buffers, swapchain or
offscreen target, submission, presentation, and device-loss policy. A compiled
program and graph may be destroyed after a batch is created because the batch
copies everything it needs.

The public entry point is `include/binblock_sdl_gpu.h`. Unsupported lowering
returns the item index, graph node, and node kind. The adapter deliberately does
not hide a CPU fallback; the host can render that item through libbinblock's
software renderer or choose a different batch boundary.

Not yet lowered:

- decoded assets and masks;
- arbitrary transform or composite depth;
- Lanczos resizing of gradients and composites;
- reference-profile rounding and table-level alpha fields.

## Current SDL on desktop

SDL 3.2 or newer, `glslangValidator`, and a native SDL GPU driver are enough for
the SPIR-V path:

```sh
cmake -S . -B .build/sdl-gpu -G Ninja \
  -DBB_BUILD_SDL_GPU=ON -DBB_BUILD_TESTS=ON
cmake --build .build/sdl-gpu
ctest --test-dir .build/sdl-gpu --output-on-failure
```

The Linux test uses Xvfb and Mesa's Lavapipe ICD when both are installed. It
compares every channel of all 256 generated variants with the CPU result and
allows only a one-byte rounding difference.

## SDL WebGPU PR #16020

The WGSL path is developed against
[SDL pull request #16020](https://github.com/libsdl-org/SDL/pull/16020). Pinning
the checkout makes this reproducible while the work is still outside SDL main:

```sh
git clone https://github.com/libsdl-org/SDL.git ~/repos/personal/contrib/SDL-webgpu
git -C ~/repos/personal/contrib/SDL-webgpu fetch origin pull/16020/head:pr-16020
git -C ~/repos/personal/contrib/SDL-webgpu switch --detach pr-16020
```

The native PR build also needs Dawn. The tested pair is SDL commit
`c6fdb20d6f8fc8d6ed63998958a37b574dabffbe` and Dawn release
`v20260819.212523` (`e463578aa391834c1cf889ef0024e9978057f39a`). With that
release unpacked under `$DAWN_RELEASE`, set the two SDL paths and build it:

```sh
SDL_WEBGPU_SOURCE=~/repos/personal/contrib/SDL-webgpu
SDL_WEBGPU_BUILD=~/repos/personal/contrib/SDL-webgpu-build

cmake -S "$SDL_WEBGPU_SOURCE" -B "$SDL_WEBGPU_BUILD" -G Ninja \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF \
  -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_X11_XTEST=OFF \
  -DSDL_WEBGPU=ON -DDawn_DIR="$DAWN_RELEASE/lib64/cmake/Dawn"
cmake --build "$SDL_WEBGPU_BUILD"

cmake -S . -B .build/sdl-gpu-webgpu -G Ninja \
  -DBB_BUILD_SDL_GPU=ON -DBB_BUILD_TESTS=ON \
  -DBB_SDL_GPU_TEST_DRIVER=webgpu \
  -DBB_SDL_GPU_WEBGPU_PR_READBACK_WORKAROUND=ON \
  -DSDL3_DIR="$SDL_WEBGPU_BUILD"
cmake --build .build/sdl-gpu-webgpu
```

`BB_SDL_GPU_TEST_DRIVER` defaults to `vulkan` for an upstream SDL build. Set it
to `webgpu` for the PR build so CTest exercises the backend being evaluated.

PR #16020 currently uses a texture region's array-layer field as WebGPU's copy
depth. A normal 2D readback therefore copies zero layers. The named workaround
changes only the differential test's readback region; it does not affect the
adapter or browser presentation and should be removed when the PR fixes that
copy.

Run the native WGSL comparison with:

```sh
xvfb-run -a env \
  LD_LIBRARY_PATH="$SDL_WEBGPU_BUILD" \
  SDL_GPU_DRIVER=webgpu \
  .build/sdl-gpu-webgpu/binblock-sdl-gpu-smoke \
  examples/generated-set.binscript
```

## Browser demo

The PR can use Emscripten's `emdawnwebgpu` port, so the adapter and WGSL shader
do not need a separate browser graphics implementation:

```sh
source /path/to/emsdk/emsdk_env.sh

SDL_WEBGPU_SOURCE=~/repos/personal/contrib/SDL-webgpu
SDL_WEBGPU_WASM_BUILD=~/repos/personal/contrib/SDL-webgpu-build-wasm

emcmake cmake -S "$SDL_WEBGPU_SOURCE" -B "$SDL_WEBGPU_WASM_BUILD" -G Ninja \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
  -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
  -DSDL_WEBGPU=ON -DSDL_WEBGPU_EMSCRIPTEN=ON
cmake --build "$SDL_WEBGPU_WASM_BUILD"

emcmake cmake -S . -B .build/sdl-gpu-wasm -G Ninja \
  -DBB_BUILD_SDL_GPU=ON -DBB_BUILD_SDL_GPU_BROWSER_DEMO=ON \
  -DBB_BUILD_TESTS=OFF -DBB_BUILD_CLI=OFF \
  -DSDL3_DIR="$SDL_WEBGPU_WASM_BUILD"
cmake --build .build/sdl-gpu-wasm
python3 -m http.server 8000 --directory .build/sdl-gpu-wasm
```

Open `http://localhost:8000/binblock-sdl-gpu-demo.html` in a WebGPU-capable
browser. The demo compiles `examples/generated-set.binscript`, lowers all 256
variants, and presents them in one SDL GPU draw. The demo target enables
Emscripten Asyncify because PR #16020 synchronously waits for WebGPU adapter and
device futures during `SDL_CreateGPUDevice`; Emdawn cannot service that wait on
the browser main thread without suspending Wasm.

WebGPU also requires a secure browser context. `http://localhost` is treated as
secure for local development, but a remote IP address must be served over HTTPS.
