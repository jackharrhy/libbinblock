# Building libbinblock

The portable core is a C11 static library with no platform or graphics
dependencies. Optional Wasm, Godot, and Wii targets consume its public ABI.

Supported native development compilers are Clang 14 or newer, GCC 11 or newer,
and MSVC 19.34 or newer. CMake 3.20 or newer is required. The public API is C11
and can be included from C++.

Configure, build, and run the native tests:

```sh
cmake -S . -B .build/native -G Ninja
cmake --build .build/native
ctest --test-dir .build/native --output-on-failure
```

For a native sanitizer build with Clang or GCC:

```sh
cmake -S . -B .build/sanitize -G Ninja -DBB_ENABLE_SANITIZERS=ON
cmake --build .build/sanitize
ctest --test-dir .build/sanitize --output-on-failure
```

`BB_BUILD_TESTS=OFF` builds only the library. `BB_BUILD_BINSCRIPT=OFF` removes
the frontend and semantic compiler for direct-graph-only consumers; CLI, Wasm,
Godot, Wii, and the test suite require the frontend.
`BB_BUILD_SHARED=ON` builds the same ABI as a visibility-controlled shared
library; static remains the default for consoles and embedding hosts.

A 32-bit compile-only portability gate can be run where the compiler supports
`-m32`:

```sh
cmake -S . -B .build/32bit -G Ninja \
  -DBB_BUILD_TESTS=OFF -DBB_BUILD_CLI=OFF -DCMAKE_C_FLAGS=-m32
cmake --build .build/32bit
```

## WebAssembly and browser

Activate Emscripten, then run:

```sh
npm run test:wasm
npm run build
```

The first command builds the integer-handle ABI and runs its Node smoke test.
The second emits the production notebook under `dist/`. The browser host uses
WebGL2 for supported graph regions, checks its declared tolerance against the
CPU result, and falls back on unsupported nodes or context loss.

For the complete reference program, the Wasm host first registers all 4,312
logical assets as metadata and compiles without raster payloads. Selecting a
bounded preview then fetches, verifies, decodes, and hydrates only the content
IDs traversed from those graph roots. `npm run build:wasm:web` emits the smaller
standalone Wasm/WebGL2 smoke harness under `.build/wasm-site/`.

## Godot and Wii

The Godot 4 GDExtension is opt-in and requires an official compatible
`godot-cpp` checkout:

```sh
cmake -S . -B .build/godot -G Ninja -DBB_BUILD_GODOT=ON \
  -DGODOT_CPP_ROOT=/path/to/godot-cpp -DBB_BUILD_TESTS=OFF -DBB_BUILD_CLI=OFF
cmake --build .build/godot --target binblock-godot
```

See `integrations/godot/README.md` for the importable `BinProgram`/`BinTexture`
resources and official-engine runtime gate. See `integrations/wii/README.md` for
the BBM, CPU-bake, GX-tile, upload-callback path, libogc2 build, and Dolphin/Wii
runtime gate.

## Fuzzing

Native Clang builds expose libFuzzer entry points for arbitrary BinScript and
serialized-module bytes:

```sh
cmake -S . -B .build/fuzz -G Ninja -DBB_BUILD_FUZZERS=ON \
  -DBB_BUILD_TESTS=OFF -DBB_BUILD_CLI=OFF
cmake --build .build/fuzz
for target in syntax semantic bbm graph collection raster; do
  .build/fuzz/binblock-fuzz-$target -max_total_time=60
done
```

The end-to-end benchmark harness covers the starter parse/check path, lazy
enumeration of all 4,312 reference outputs, a 64×64 gradient/composite, and the
16-item preview slice of a 304-item product:

```sh
npm run benchmark:lib
```

Add `--full` through `npm run benchmark:lib:full` to include first-pass and warm
filesystem-cache packaging of all 4,312 outputs. The full case is intentionally
not part of the default test suite.

The fast CTest suite includes the analytic flat-color and gradient-mask smoke
gates. Run the explicit full archive comparison separately:

```sh
npm run conformance:reference
```

That command checks all 4,312 outputs using
`reference-set/reference-conformance.tsv`. It writes an aggregate JSON report and
a per-file TSV report under `.build/native/`. Byte aliases are compared as
encoded bytes, alpha-only contracts compare alpha, exact and pixel-alias
contracts compare every RGBA byte, and bounded contracts accept only their
declared maximum per-channel error. The report records differing unit counts,
maximum error, first mismatch, and both decoded-pixel SHA-256 values. Current
non-zero bounds are at most 13 for the recovered blue gradient slice and at most
2 for recovered Lanczos3 downscales; raster-backed contracts remain exact.

The implemented components and release matrix are summarized in
`docs/implementation-status.md`; the executable CI jobs live in
`.github/workflows/ci.yml`.
