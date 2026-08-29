# Turning Bingen into `libbinblock`

This document is the implementation handoff for rebuilding this repository around
`libbinblock`.

The short version is:

> Preserve the current repository as a behavioral oracle, but do not port its
> TypeScript architecture. Start a new portable C implementation beside it. Make
> the C library own image semantics, planning, BinScript compilation, and the
> canonical software renderer. Treat the browser notebook, command-line tools,
> Godot, WebGL/WebGPU, and Wii GX as hosts or backends around that library.

The objective is not merely to rewrite Bingen in C. It is to extract a reusable
semantic graphics runtime that can power all of these environments without making
any one of them the architectural center:

- the live BinScript notebook in a browser through WebAssembly;
- a native asset generator or packager;
- procedural textures and eventually materials in Godot;
- game graphics on conventional desktop/mobile GPUs;
- constrained fixed-function hardware such as Wii GX through `libogc2`;
- tests and offline generation through a deterministic software rasterizer.

The existing implementation is valuable evidence about the desired behavior. It
is not the shape of the future library.

## 1. What exists today

At the time this plan was written, the repository contains:

- a browser application and CodeMirror notebook in TypeScript;
- a handwritten BinScript tokenizer/parser/evaluator/lowerer in
  `src/binscript-language.ts`;
- a Zod-validated `bin-block-recipe/v1` JSON document model in
  `src/recipe-schema.ts`;
- an eager stage executor in `src/recipe-executor.ts`;
- pixel operations and recovered historic behavior split between `src/core.ts`
  and `src/legacy.ts`;
- reference-family construction and compatibility logic in
  `src/reference-set-recipe.ts` and `src/reference-set-operations.ts`;
- a build that embeds the complete reference archive and comparison fixtures in
  the browser bundle;
- 4,312 reference PNG files, about 17 MiB on disk;
- useful tests for exact pixels, source ranges, recipe validation, stage ordering,
  Cartesian expansion, reference families, aliases, and notebook behavior.

The reference set currently breaks down as follows:

| Family/folder                |    Images |
| ---------------------------- | --------: |
| `col`                        |        16 |
| `Gradient Layers Alpha Maps` |        19 |
| `col bin 2`                  |     1,202 |
| `blue 64-8 24 bit`           |        88 |
| `red FG-Alpha`               |        22 |
| `Red-col fg-alpha Print`     |       815 |
| `brown bear`                 |        65 |
| `out4 - Select Library`      |       468 |
| `out4 - modding`             |       208 |
| `out4-special`               |       385 |
| `No AA 64px Black+White`     |        26 |
| `New folder`                 |        26 |
| `result`                     |       972 |
| **Total**                    | **4,312** |

This material must be preserved. In particular, the old filenames, dimensions,
visible RGB beneath transparent pixels, alpha values, duplicate relationships,
and known exceptions are part of the behavioral specification.

## 2. Why the current approach is not the `libbinblock` architecture

The current code is a productive prototype. The problem is not that it is useless
or that every behavior is wrong. The problem is that its boundaries follow the
needs of one TypeScript browser application instead of the needs of a portable
runtime.

### 2.1 Language, compiler, notebook, and recipe construction are entangled

`src/binscript-language.ts` currently performs several jobs in one path:

1. tokenization;
2. parsing into private TypeScript interfaces;
3. name/value evaluation;
4. construction of temporary collection and image plans;
5. immediate conversion of bindings into recipe stages;
6. production of editor projection records for sliders, swatches, previews, and
   comparisons.

That makes a quick notebook possible, but it prevents the language from having a
stable syntax tree, a real semantic/type phase, reusable modules, independent
editor tooling, or a backend-neutral compiled representation. A source expression
should not become a browser preview decoration while it is being semantically
lowered.

The replacement needs distinct phases:

```text
source bytes
    -> tokens / lossless syntax with spans
    -> AST
    -> name resolution and type checking
    -> typed semantic graph and lazy collection plan
    -> render planning
    -> backend execution

AST spans + semantic results
    -> evaluation trace / editor query API
    -> notebook projections
```

### 2.2 The JSON recipe document is acting as both interchange format and core IR

`bin-block-recipe/v1` was a good way to validate ideas. It should not become the
in-memory C object model or public ABI.

The current recipe format mixes:

- scalar expressions represented as JSON tuples;
- image expression trees;
- collection expansion axes;
- dependency stages;
- output filenames and metadata;
- asset declarations;
- aliases and reproduction notes.

This makes the schema the center of every change. The closed Zod union also means
that adding an operation affects schema definitions, validation, execution, and
often BinScript lowering at once. More importantly, JSON-shaped objects are a poor
runtime representation for a console, a game engine hot path, or a compact Wasm
module.

The new core should use immutable, typed nodes with compact IDs/handles. A
versioned serialized form can be designed later from that semantic model. The
existing recipe format may remain temporarily as:

- a migration importer;
- a debugging/export view;
- a source of old-vs-new differential tests.

It must not constrain the new core.

### 2.3 Compilation eagerly renders collections

`compileRecipe()` walks every expanded stage combination and stores an RGBA image
for every artifact. `expandStageBindings()` constructs Cartesian products as
arrays. This is acceptable for small tests but is the wrong execution model for:

- a 4,312-output notebook with bounded previews;
- a game that changes one parameter every frame;
- memory-constrained console hardware;
- GPU execution where intermediate images should stay on-device;
- an exporter that could pass through an unchanged PNG without decoding it.

Collection planning and image materialization must be separate. The library must
be able to answer “how many outputs exist?”, “what are their keys?”, and “render
items 100 through 115” without rendering items 0 through 99.

### 2.4 Image semantics depend on JavaScript and host libraries

The current implementation uses JavaScript numbers, typed arrays, Node Canvas,
browser Canvas, and host image decoding. Those are fine adapters, but they cannot
define portable rendering semantics.

Exact behavior needs a specification for:

- stored pixel format and channel order;
- straight versus premultiplied alpha;
- color space and whether operations use numeric sRGB or linear light;
- gradient sample locations;
- interpolation and easing;
- clamping and integer rounding at every operation;
- source-over arithmetic;
- resize filter coefficients and edge behavior;
- the RGB values retained when alpha is zero;
- aliases, decoded-pixel equality, and encoded-byte equality.

The C software renderer should become the conformance implementation. GPU
backends may offer exact, bounded-error, or visual equivalence depending on the
operation and target. They must not silently redefine the result.

### 2.5 The browser build treats the reference archive as application code

`build.ts` reads all 4,312 PNGs, converts them to base64, creates an atlas, and
injects the payload into the JavaScript bundle. Base64 alone inflates the data,
and this approach guarantees that the browser pays for the entire archive before
the notebook can use a small preview.

Reference assets are data. A host must resolve them lazily by logical asset ID,
path, content hash, package entry, HTTP URL, Godot resource, disc file, or ROM
offset. The portable library should know only about an asset-resolver callback.

### 2.6 Historic recovery logic is mixed with generic rendering

`src/legacy.ts` contains both generally useful operations and knowledge specific
to this archive: recovered constants, filename rules, corrections, special layer
recipes, and aliases. That work is valuable, but “legacy” must not become the core
rendering API.

Each recovered rule should end in one of three places:

- a generic operation with fully documented semantics;
- a `binblock/reference-set` compatibility module expressed using generic
  operations;
- a pinned raster asset plus metadata when the original process is not yet known.

Do not add archive-specific operation names to the public core merely to claim
that every fixture is analytic.

### 2.7 There are currently two overlapping notions of a renderer

`src/core.ts` implements simple presets and blocks while `src/legacy.ts` contains
the richer gradient, composition, transform, and resize machinery. The C rebuild
must establish one canonical operation set and one canonical CPU implementation.
Compatibility behavior should be selected by a documented render profile or
explicit node options, not by calling a separate miscellaneous renderer.

### 2.8 A generic parser framework or generic GPU wrapper would repeat the mistake

`libbinblock` should not attempt to compete with parser generators, graphics APIs,
or general game engines.

- Its parser should parse BinScript and retain the source information BinScript
  tooling needs. Shared utilities such as arenas, interners, and diagnostics are
  useful; a general-purpose parser framework is not a project goal.
- Its renderer should expose semantic image/material operations. It should not
  invent a lowest-common-denominator replacement for WebGPU, OpenGL, Godot's
  renderer, and GX.
- Backend code should lower supported semantic operations and report capabilities.
  Unsupported subgraphs can be baked with the CPU renderer and uploaded as
  textures.

The reusable value is the semantic graph and its defined results, not a new syntax
toolkit or GPU API.

## 3. Target architecture

The long-term data flow should be:

```text
                       BinScript source
                              |
                lexer -> AST -> semantic analysis
                              |
                     typed module / graph
                              |
          +-------------------+-------------------+
          |                                       |
   lazy artifact plan                    material/field program
          |                                       |
          +-------------------+-------------------+
                              |
                       render planner
                              |
            +-----------------+------------------+
            |                 |                  |
     reference CPU       WebGL/WebGPU       engine/console
       rasterizer           backend            backends
            |                 |                  |
       RGBA surfaces     GPU resources      Godot / Wii GX
```

There are four important separation lines:

1. **BinScript is a frontend.** The C API can also construct the same semantic
   graph directly without parsing text.
2. **The semantic graph is not a raster surface.** It describes values, images,
   collections, parameters, assets, outputs, and eventually dynamic fields.
3. **The render planner is not a GPU API.** It decides which nodes a backend can
   execute, where intermediates live, and when CPU baking/readback is required.
4. **Hosts own platform services.** Files, clocks, threads, windows, input, PNG
   codecs, Godot resources, GX objects, and JavaScript objects do not enter the
   core ABI.

### 3.1 Proposed component boundaries

#### `binblock-core`

Portable C11 with no required platform or graphics dependency:

- context, allocator, limits, logging callback, and error handling;
- strings, source IDs, spans, diagnostics, and immutable handles;
- scalar/vector/color/image/collection type descriptions;
- immutable semantic graph nodes;
- parameter schemas and values;
- lazy artifact plans, keys, paths, aliases, and provenance;
- asset references and resolver contracts;
- graph validation, hashing, dependency analysis, and lifetime management;
- backend capability and execution contracts.

Avoid POSIX calls, filesystem access, locale-dependent parsing, global mutable
state, required threads, C bitfields in public data, and serialized native struct
layouts.

#### `binblock-binscript`

Still portable C and usually built with the core:

- lexer and parser;
- lossless tokens/trivia or another representation adequate for editor ranges;
- syntax AST with source spans and error nodes;
- module/import declarations;
- name resolution and type checking;
- constant evaluation;
- standard-library binding;
- lowering to the core semantic graph;
- per-node semantic/evaluation trace queries for notebook tooling.

The parser can begin as handwritten recursive descent. Keep its input/output
boundary clean enough that an incremental editor parser could be added later
without changing the runtime graph.

#### `binblock-raster`

The canonical software backend:

- deterministic `RGBA8` surfaces first;
- fills and gradients;
- crop, canvas/placement, quarter-turn rotation, opacity, alpha inversion, tint,
  two-color remap, masking, and source-over;
- the specified Lanczos3 path;
- explicit render profiles for modern and reference-compatibility behavior;
- tile/scanline execution where practical so an entire graph need not be resident;
- optional decoded-raster cache supplied through the context.

This may ship in the same library initially, but it must use the same backend
contract that later backends implement.

#### Codecs and asset adapters

PNG/JPEG decoding, font rasterization, filesystem access, network loading, and ZIP
packaging are adapters. They should be optional and replaceable.

The core deals in byte spans, decoded image views, and logical assets. A CLI may
use one PNG library, the browser may use browser APIs or a Wasm codec, Godot may
use `Image`, and a console build may use preconverted textures.

#### Hosts and integrations

- `binblock-cli`: compile, inspect, render, compare, package, and precompile.
- `binblock-wasm`: a small exported ABI and JavaScript/TypeScript wrapper.
- `bingen-web`: CodeMirror, controls, previews, persistence, and export UI.
- `binblock-godot`: GDExtension resources, importer, inspector plugin, caching,
  and rendering bridge.
- `binblock-wii`: `libogc2` host services, GX lowering, texture conversion, and
  offline/precompiled module loading.

Hosts may share helper code, but no host is allowed to become the definition of
the language or pixel semantics.

## 4. Public API rules

Do not freeze every function on day one, but adopt these rules before implementation:

- Prefix public symbols with `bb_` and public macros with `BB_`.
- Expose opaque handles, not internal struct layouts.
- Every owned object has an explicit lifetime. Prefer context-owned immutable
  objects or retain/release handles; do not mix the models unpredictably.
- Accept an allocator in the context configuration. Never require `malloc` to be
  the host's only choice.
- Return status codes and structured diagnostics. Do not use `errno`, `longjmp`,
  globals, or printing as error propagation.
- Use fixed-width integer types for serialized and pixel-visible values.
- Use pointer-plus-length strings/byte spans, not null termination as the only
  representation.
- Every collection or source-controlled expansion has configurable limits.
- The library does not open files. Imports and assets go through callbacks.
- Callbacks receive an explicit user-data pointer.
- Do not retain borrowed host memory unless the contract explicitly says so.
- Keep the ABI independent of C++ exceptions, RTTI, JavaScript, Godot, and GPU
  headers.
- Never serialize a C struct by copying its bytes. The Wii target makes endianness
  and alignment bugs non-theoretical.

An early API sketch—not a promise of exact names—is:

```c
typedef struct bb_context bb_context;
typedef struct bb_module bb_module;
typedef struct bb_program bb_program;
typedef struct bb_backend bb_backend;

typedef struct {
    void *user;
    void *(*alloc)(void *user, size_t size, size_t align);
    void *(*realloc)(void *user, void *ptr, size_t old_size,
                     size_t new_size, size_t align);
    void (*free)(void *user, void *ptr, size_t size, size_t align);
} bb_allocator;

typedef struct {
    const uint8_t *data;
    size_t length;
} bb_bytes;

typedef struct {
    uint32_t source_id;
    uint32_t byte_start;
    uint32_t byte_end;
} bb_span;

bb_status bb_context_create(const bb_context_desc *desc, bb_context **out);
bb_status bb_compile(bb_context *ctx, const bb_compile_desc *desc,
                     bb_module **out_module);
size_t bb_module_diagnostic_count(const bb_module *module);
bb_status bb_module_diagnostic(const bb_module *module, size_t index,
                               bb_diagnostic *out);
bb_status bb_module_link(bb_context *ctx, const bb_module *module,
                         bb_program **out_program);
bb_status bb_program_output_count(const bb_program *program, uint64_t *out_count);
bb_status bb_program_output_info(const bb_program *program, uint64_t index,
                                 bb_output_info *out_info);
bb_status bb_render_output(bb_context *ctx, const bb_program *program,
                           uint64_t index, bb_backend *backend,
                           const bb_render_desc *desc, bb_surface *out_surface);
```

The first vertical slice can be much smaller. The important point is that the
notebook and CLI call the same API and receive the same diagnostics, output
descriptions, and pixels.

## 5. Semantic model decisions to make explicitly

Write short architecture decision records under `docs/decisions/` for these
questions. Do not let answers emerge accidentally from an implementation detail.

### 5.1 Values and types

The initial type set should include at least:

- `bool`;
- integer and finite scalar number;
- unit-bearing values needed by the language, initially degrees and percentages;
- string and interned symbol;
- RGBA color with preserved RGB at zero alpha;
- two-dimensional vector/point;
- image expression;
- asset reference;
- ordered `collection<T>`;
- callable/standard-library function;
- output/artifact description.

Do not represent all values as strings, JSON, or untagged unions. Define conversion
rules and issue type diagnostics before render planning.

### 5.2 Collections and lifting

Collection behavior is a defining feature, not a convenience hidden in stage
construction. Specify:

- stable ordering;
- key and metadata propagation;
- `map`, `flat_map`, `filter`, `collect`/concatenation, `select`, and `slice`;
- `zip` length/error behavior;
- Cartesian `product` ordering;
- whether unary image methods broadcast over collections;
- whether binary methods such as `mask` accept image/collection combinations and
  whether collection/collection means zip, product, or a type error;
- maximum cardinality, overflow handling, and unknown/infinite cardinality;
- lazy indexed access versus one-way iteration.

Do not preserve accidental semantics simply because `forEach` currently expands
object fields in JavaScript insertion order. Add tests for the chosen contract.

### 5.3 Image semantics and profiles

Define an image descriptor with explicit width, height, format, color space, alpha
mode, and row pitch. Start with `RGBA8_UNORM` stored in straight alpha, but allow
the raster backend to use a documented internal representation.

At least two profiles will likely be useful:

- a modern/default profile with clearly specified color behavior;
- `reference-set-exact/v1`, whose rounding and sampling reproduce the archive.

Do not promise pixel identity from all GPUs. Each backend/operation should report
one of:

- exact according to the selected profile;
- bounded error with a declared tolerance;
- visual-only;
- unsupported, requiring CPU bake/fallback.

### 5.4 Graph identity, caching, and parameters

Graph nodes should be immutable. Give them stable within-program IDs and a
canonical structural hash that includes semantic options and asset content IDs.
Do not include host pointers in the hash.

Separate:

- compile-time constants that change graph shape or collection cardinality;
- render parameters/uniforms that can update without recompilation;
- resource parameters such as images and palettes;
- volatile host values such as time.

This separation is required for useful Godot materials and per-frame rendering.

### 5.5 Static images versus dynamic materials

Do not pretend the existing static image graph is already a shader language.
Implement static image generation first. Then introduce a restricted, pure
coordinate-dependent field/material subset with inputs such as UV, pixel
coordinate, time, and declared uniforms.

The same semantic operation may have three executions:

- evaluated across pixels by the CPU rasterizer;
- lowered to a fragment/compute shader on WebGPU/WebGL/Godot;
- lowered to GX texture/TEV stages when possible.

When a graph cannot run dynamically on a backend, the planner may bake it into a
texture with the CPU backend. This makes the system useful on Wii without forcing
all material features into the GX fixed-function model.

### 5.6 Modules and host resolution

`import "binblock/basic"` should resolve through a module callback or built-in
module registry. The core must not infer a filesystem path.

The resolver should support:

- built-in native modules;
- source modules;
- precompiled modules;
- asset packages;
- host-specific failure/permission diagnostics;
- source identity for diagnostics and cycle detection.

Keep reference-set compatibility in a separate module. The basic module must not
contain hidden 4,312-image recipes.

## 6. Proposed repository shape

Build the new system beside the existing code until it proves parity:

```text
bingen/
  CMakeLists.txt
  cmake/
    toolchains/
  include/binblock/
    binblock.h
    binscript.h
    backend.h
    raster.h
  lib/
    core/
    binscript/
    raster/
    codecs/              # optional adapters, not core requirements
  modules/
    basic/
    reference-set/
  tools/
    binblock/
    reference-manifest/
  bindings/
    wasm/
    javascript/
  integrations/
    godot/
    wii/
  web/                    # eventual notebook shell
  tests/
    unit/
    language/
    conformance/
    differential/
    fuzz/
  docs/
    language/
    render-profiles/
    decisions/
  reference-set/         # preserve in place
  reference-manifest.*   # generated inventory and hashes
  legacy-ts/             # optional move only after the C path works
  src/ test/ build.ts     # leave untouched during the initial C work
```

The exact directory names are flexible. The boundaries are not.

Use CMake initially because it can drive native, Emscripten, Godot-extension, and
devkitPro-style toolchains. Keep the actual library sources independent of CMake
so another build system can consume them later.

Recommended build options include:

```text
BB_BUILD_BINSCRIPT
BB_BUILD_RASTER
BB_BUILD_CLI
BB_BUILD_TESTS
BB_BUILD_WASM
BB_BUILD_GODOT
BB_BUILD_WII
BB_ENABLE_SANITIZERS
```

Default native development should build the core, BinScript frontend, raster
backend, CLI, and tests. Platform integrations remain opt-in.

## 7. Step-by-step implementation plan

Every step below has a completion gate. Do not start by translating all 6,000+
lines of TypeScript or by making the full notebook compile.

### Step 0: Freeze the oracle and inventory the archive

Before changing architecture:

1. Preserve the current TypeScript state in a branch or tag. The repository may
   have uncommitted work, so resolve and commit that intentionally rather than
   mixing it into the C bootstrap.
2. Generate a deterministic reference manifest containing, for every file:
   - normalized relative path;
   - byte length and SHA-256 of encoded bytes;
   - decoded width, height, pixel format, and alpha presence;
   - SHA-256 of canonical decoded RGBA8 pixels;
   - family classification;
   - known alias target if applicable.
3. Store or generate the family counts listed above and assert the 4,312 total.
4. Record known equivalence status: pixel-exact, alpha-only exact, bounded
   difference, raster fallback, pixel alias, or byte alias.
5. Add a small, hand-curated “smoke fixtures” set for fast tests. Keep the complete
   archive for full conformance runs.

**Gate:** One command can inventory the archive twice with byte-identical manifest
output, and tests fail if paths, counts, dimensions, hashes, or known aliases drift.

### Step 1: Bootstrap the portable C project

Add the build without removing or routing through TypeScript:

1. Compile as a conservative C11 subset.
2. Avoid VLAs, C11 threads, platform atomics, compiler-specific struct packing,
   and reliance on `char` signedness.
3. Create a static library first and make shared-library visibility explicit.
4. Add a tiny C test runner or a minimal vendored test dependency.
5. Enable warnings-as-errors for the project's own code in CI, but not for host
   SDK headers.
6. Add AddressSanitizer and UndefinedBehaviorSanitizer native jobs.
7. Add compile-only Emscripten and big-endian/toolchain jobs as soon as those
   toolchains are available.
8. Document supported compilers and the minimum language version.

**Gate:** A no-op context can be created and destroyed in native and Wasm builds,
with allocation-failure tests passing and no platform headers in public core
headers.

### Step 2: Implement the foundation and diagnostics

Implement only what later phases require:

- allocator wrapper and overflow-checked size arithmetic;
- arena/scratch allocation for compile phases;
- owned and borrowed byte/string views;
- UTF-8 validation policy;
- source registry and byte spans;
- diagnostic severity, code, message, primary span, and related spans;
- typed opaque handles or IDs;
- deterministic string interning;
- configurable safety limits;
- context lifetime and error/status rules.

Diagnostics must be data. Formatting a diagnostic for a terminal or CodeMirror is
a host concern.

**Gate:** Unit tests cover zero-length input, invalid UTF-8 policy, allocation
failure at every allocation point, size overflow, source ranges, and deterministic
diagnostic order.

### Step 3: Specify pixels before porting render code

Write `docs/render-profiles/reference-set-exact-v1.md` before implementing the
renderer. Extract the behavior demonstrated by `core.ts`, `legacy.ts`, and the
fixtures:

- coordinate normalization and pixel-center convention;
- color parsing and preservation of transparent RGB;
- alpha representation;
- clamp/round order;
- source-over formula;
- gradient stop interpolation and easing;
- axial, Euclidean, Chebyshev, border, and ellipse distance fields;
- crop and out-of-bounds fill;
- canvas placement;
- quarter-turn rotation dimension rules;
- opacity, mask modes, visible-RGB replacement, chroma tint, and two-color remap;
- Lanczos3 coefficient generation, sampling support, normalization, edge
  behavior, and final rounding.

Where the TypeScript and a fixture disagree, the fixture and explicitly recorded
compatibility intent win. Do not depend on the host `libm` producing identical
last-bit results. For exact-profile paths, prefer specified integer/fixed-point
steps or carefully bounded scalar routines.

**Gate:** Another implementer could reproduce a pixel operation from the document
without reading TypeScript.

### Step 4: Build the canonical software rasterizer

Port operations one at a time, in this order:

1. surface allocation/view validation and clear/fill;
2. crop, canvas placement, and 90-degree rotation;
3. opacity, alpha inversion, visible-RGB replacement, and tint/remap;
4. source-over composition;
5. mask multiply and replace;
6. linear gradient and stop interpolation;
7. Euclidean, Chebyshev, border-distance, and ellipse fields;
8. Lanczos3 resize;
9. decoded raster input and unchanged-asset passthrough metadata;
10. tile/scanline execution where it does not alter semantics.

After each operation, compare C output against hand-written tiny cases and selected
reference PNGs. Do not wait for the language parser.

PNG decoding/encoding should initially live in the test/CLI adapter. The raster
core accepts and returns described surfaces.

**Gate:** The C renderer reproduces flat colors and gradient masks `00` through
`17` according to their current exactness contract, with the compatibility path
for `18` explicitly identified rather than hidden in a generic operation.

### Step 5: Add the immutable semantic image graph

Create typed constructors for generic image nodes. Initially support only the
operations already implemented by the rasterizer.

Requirements:

- node construction validates types and ranges;
- nodes are immutable after publication;
- child relationships form a validated acyclic graph;
- structurally equal nodes can be interned/deduplicated;
- graph traversal is iterative or depth-limited;
- every node has provenance/source-span attachment separate from semantic
  identity;
- resource and parameter references are explicit;
- graph serialization is deferred until the in-memory model settles.

Expose C constructors so a game or engine can build a graph without BinScript.

**Gate:** A C-only program constructs `fill -> gradient/mask -> composite ->
resize`, renders it through the software backend, and gets the expected result
without any parser or JSON document.

### Step 6: Add lazy artifact and collection planning

Implement collection values above the image graph:

- ordered values and keyed records;
- lazy `map`, `filter`, `flat_map`, concatenation, `zip`, Cartesian `product`,
  keyed selection, and slice;
- cardinality calculation with checked 64-bit arithmetic;
- indexed enumeration where possible;
- stable key/path generation;
- per-artifact metadata and provenance;
- aliases that can point to recipes, decoded pixels, or encoded bytes;
- bounded preview enumeration;
- render-on-demand and cache policy hooks.

Do not allocate an array proportional to a Cartesian product merely to count or
inspect it. A render request for output N should materialize only the dependency
path needed for N, except where an operation inherently requires aggregation.

**Gate:** A 16 x 19 collection reports 304 outputs immediately, enumerates a
requested slice in stable order, and renders only that slice. A synthetic product
larger than addressable memory fails by cardinality/limit rather than allocation
or integer overflow.

### Step 7: Implement BinScript syntax as an independent frontend

Start with the syntax already demonstrated in `BINSCRIPT.md` and tests:

- imports;
- immutable `:=` bindings;
- number, degree, percent, string, color, and array literals;
- identifiers;
- calls with positional and named arguments;
- member calls;
- standalone display/output expressions;
- comments and trailing commas.

Parser requirements:

- retain byte ranges for every token and AST node;
- keep comments/trivia if formatting and stable editor behavior require them;
- create error nodes or synchronize after common errors instead of aborting after
  the first invalid token;
- never execute arbitrary C, JavaScript, or host code;
- place syntax structures in a parser/AST layer, not directly in graph nodes;
- test malformed and incomplete notebook input as heavily as complete programs.

The current TypeScript parser is a syntax guide, not code to transliterate. Its
private AST and immediate lowering are precisely what this phase is meant to
replace.

**Gate:** The frontend parses a representative starter program, returns multiple
ordered diagnostics for malformed input, and can expose exact spans without
constructing or rendering an image graph.

### Step 8: Add semantic analysis, standard functions, and evaluation traces

Build a separate semantic pass:

1. resolve imports and bindings;
2. detect duplicate names and cycles;
3. infer/check value types and units;
4. bind calls against registered standard-library signatures;
5. constant-fold only where semantics permit;
6. lower image expressions to graph nodes;
7. lower collection expressions to lazy plans;
8. declare outputs from standalone expressions or explicit export declarations;
9. attach source provenance and build a semantic trace index.

The trace index should answer notebook questions such as:

- What type and value did this expression produce?
- Is this literal an editable color or numeric parameter?
- What parameter constraints apply?
- Which output collection is associated with this standalone expression?
- Which diagnostics and graph nodes originated in this range?

The C compiler must not emit CodeMirror decorations. The Wasm binding converts
trace queries into notebook data.

**Gate:** This program compiles through the C path and renders exactly through the
software backend:

```binscript
import "binblock/basic"

size := 64
colors := palette(red: #ff0000, blue: #0000ff)
blocks := colors.map(fill).size(size)
fade := lg(180deg, white, transparent-white).size(size)
outputs := blocks.mask(fade)
outputs
```

The chosen collection/collection mask semantics must be stated in a decision
record and asserted by tests.

### Step 9: Implement host-resolved modules and assets

Add:

- a built-in `binblock/basic` module containing generic functions and types;
- source and precompiled module resolver callbacks;
- logical asset IDs with optional content hash, dimensions, and type constraints;
- decoded-image and encoded-byte resolver callbacks;
- import and asset cycle diagnostics;
- cache keys derived from content identity, not filesystem path alone;
- a separate `binblock/reference-set` module for irreducible compatibility data.

The reference-set module may contain mappings, constants, and raster assets. It
must not hide generic recipes that should be readable BinScript.

**Gate:** The same compiled source resolves assets from an in-memory native test,
the CLI filesystem adapter, and a browser callback without core changes.

### Step 10: Create the native CLI and differential harness

Build a deliberately plain CLI before reconnecting the notebook. It should be
able to:

- parse/check a BinScript file;
- print diagnostics with source locations;
- list parameters, outputs, keys, dimensions, dependencies, and provenance;
- render one output or a range;
- compare rendered pixels with a fixture directory;
- emit a machine-readable comparison report;
- render/package all outputs with limits and cancellation;
- inventory the reference set;
- optionally import the old recipe JSON for differential testing.

Run the old TypeScript implementation and the new C implementation against a
shared corpus. Compare semantic output order, keys, paths, metadata, dimensions,
and pixels—not internal data structures.

**Gate:** CI can run a fast smoke suite, while an explicit full-conformance command
checks the complete archive and writes a useful mismatch report.

### Step 11: Migrate the reference families in increasing difficulty

Move family semantics into visible BinScript and generic operations in this order:

1. 16 flat colors;
2. 19 gradient masks;
3. the currently recovered `grad00`-`grad04` variant slice;
4. all 1,202 gradient variants as formulas are recovered;
5. 88 downscaled images using canonical upstream graph nodes;
6. 22 foreground-alpha fields;
7. 815 foreground composites;
8. 65 organic/elliptical gradients;
9. 1,061 layer compositions across the three `out4` families;
10. 26 sans and 26 serif glyphs after font files and rasterization settings are
    identified and pinned;
11. 972 ordered results, preserving the known 960 pixel aliases and 12 raster
    exceptions.

For each family:

1. express known behavior using generic nodes;
2. declare compatibility assets for unknown behavior;
3. compare every output against the manifest;
4. publish a per-file exactness/provenance report;
5. remove a raster dependency only after the analytic path satisfies its declared
   gate;
6. keep output paths stable unless an intentional versioned format change is
   recorded.

Do not block the library architecture on reverse-engineering every old image.
Raster-backed nodes are a legitimate, explicit intermediate state.

**Gate:** One BinScript program can lazily describe all 4,312 outputs, enumerate
them without decoding all assets, preview a bounded subset, and reproduce the
manifest according to each file's declared equivalence class.

### Step 12: Compile to WebAssembly and replace the notebook compiler

The first Wasm integration should use the CPU backend and keep the UI in
TypeScript:

- compile source and return structured diagnostics;
- query semantic trace/projection information;
- enumerate output collections and bounded slices;
- set parameter values;
- request a raster preview into Wasm memory or a host-provided buffer;
- resolve imports/assets asynchronously through a bridge where necessary;
- cancel or supersede stale compile/render requests;
- expose no raw internal pointers to long-lived JavaScript code.

Replace the TypeScript parser, recipe lowering, and executor only after the C/Wasm
path passes differential tests. Keep CodeMirror, DOM controls, canvas display, and
browser persistence in the web host.

Fetch reference files or packages on demand. Do not reproduce the current
all-images-as-base64 bundle.

**Gate:** The browser notebook uses the same compiled C library as the CLI, opens
quickly without embedding the full archive, reports source-ranged diagnostics,
and renders the starter program with matching pixels.

### Step 13: Define backend lowering only after CPU semantics are stable

Add a backend capability model and a small render-planning representation. A
backend should be able to report:

- supported node kinds and option subsets;
- supported surface formats and limits;
- exactness class per operation/profile;
- maximum texture dimensions and resource counts;
- dynamic field/material support;
- readback and upload support;
- preferred tiling/alignment.

The planner partitions a graph into supported regions. It may:

- execute a region directly on the backend;
- bake an unsupported region with the CPU rasterizer and upload the result;
- reuse a cached intermediate by structural hash;
- reject execution if fallback is disabled or memory limits are exceeded.

WebGPU, WebGL, Godot, and GX should each lower from this semantic/planning layer.
Do not force them to implement a pretend universal command buffer.

**Gate:** A test backend with deliberately limited capabilities causes a mixed
graph to partition predictably, uses CPU fallback where expected, and produces the
same final image within its declared equivalence class.

### Step 14: Add WebGL/WebGPU acceleration

Start with operations that map naturally to fragment or compute work:

- fills and gradients;
- affine sampling, crop, placement, rotation, and resize;
- masking and source-over;
- simple field/material evaluation.

In a browser, the Wasm core may call a narrow JavaScript host bridge rather than
owning browser GPU objects directly. Keep shader generation/lowering testable
outside the UI. Cache pipelines and resources by semantic hash plus backend
configuration.

Always retain the CPU path for conformance, headless use, small images, and
unsupported nodes.

**Gate:** GPU previews agree with CPU results at the documented tolerance, and
backend loss/context reset does not corrupt the compiled program.

### Step 15: Add the Godot integration

Build Godot as a consumer of the stable C ABI, not as a special compilation mode
inside the core.

An initial useful integration is:

- importer for `.binscript` and/or precompiled BinBlock modules;
- `BinProgram` resource containing compiled program data and diagnostics;
- texture resource/wrapper selecting a named output plus parameter values;
- editor inspector generated from the parameter schema;
- preview thumbnails and dependency tracking;
- cache invalidation when source, import, asset, or parameter identity changes;
- CPU-baked `ImageTexture` fallback.

Then add a material-facing subset:

- define which BinScript field nodes are legal dynamically;
- lower them to a Godot shader or RenderingDevice path where practical;
- use declared uniforms for live parameter changes;
- bake unsupported graphs to textures;
- keep Godot `RID`, `Resource`, `Image`, and rendering types out of public core
  headers.

**Gate:** A Godot project imports one BinScript file, displays its compile
diagnostics in the editor, exposes parameters in the Inspector, uses its output on
a 2D or 3D resource, and updates without rebuilding `libbinblock`.

### Step 16: Add the Wii/`libogc2` integration

Treat Wii as an architectural test of portability and bounded execution:

- provide a host allocator suitable for constrained memory;
- use host callbacks for disc/SD/package access;
- precompile BinScript offline when runtime parsing is unnecessary;
- serialize modules with explicit byte order and versioning;
- convert/cache textures in GX-friendly layouts;
- lower supported blend/texture/TEV combinations;
- bake unsupported image/material subgraphs on CPU or offline;
- avoid readback-heavy designs;
- test big-endian behavior and alignment assumptions;
- put platform and GX code entirely in the Wii integration/backend.

Do not require the Wii to implement the full dynamic material subset. A small GX
path plus robust texture baking still validates that the core is genuinely
portable.

**Gate:** The same precompiled semantic program used by a native test can be loaded
on the Wii host, enumerate a bounded output set, and display at least one generated
or baked texture without host-specific changes to core graph semantics.

### Step 17: Retire the prototype paths deliberately

Only after the C/Wasm notebook is established:

- remove the TypeScript BinScript compiler from production builds;
- remove the Zod recipe executor from production builds;
- keep a recipe importer only if users or useful fixtures require it;
- keep TypeScript analysis scripts that remain valuable, or rewrite them as CLI
  commands when justified;
- move old code into a clearly marked legacy location or delete it after a tag
  preserves history;
- update `README.md`, `BINSCRIPT.md`, and `TODO.md` to describe the C-owned path;
- rename packages and headers consistently around BinBlock while retaining
  Bingen as the notebook/application name if desired.

**Gate:** No production host contains a second parser, evaluator, renderer, or
collection semantics implementation.

## 8. Suggested first pull requests

The steps above are architectural phases. The first implementation work should be
kept reviewable with roughly this PR sequence:

1. Reference manifest generator and locked 4,312-file inventory.
2. CMake skeleton, context/allocator, status codes, and native tests.
3. Sources, spans, diagnostics, strings, limits, and failure injection.
4. Surface descriptors plus fill/crop/rotate operations.
5. Composition, masks, gradients, and exact-profile documentation.
6. Lanczos3 and selected reference conformance tests.
7. Immutable image graph and direct C construction API.
8. Lazy collection/product/slice enumerator.
9. Lexer and parser with recovery, no semantic lowering yet.
10. Type checking, basic module, graph lowering, and semantic trace.
11. CLI check/list/render/compare commands.
12. Flat-color and gradient-mask BinScript conformance.
13. Wasm ABI plus a minimal non-CodeMirror browser harness.
14. Existing notebook switched to C/Wasm behind an adapter.

Do not combine “new C core,” “full language,” “4,312-image parity,” and “new web
UI” in one change.

## 9. Testing and quality requirements

### Unit tests

Cover allocation failure, overflow, invalid handles, invalid dimensions, graph
cycles, type errors, parser recovery, spans, limits, every pixel operation, and
collection ordering.

### Golden language tests

Store source plus expected diagnostics, typed outputs, collection keys, and a
stable human-readable graph dump. Avoid raw native struct snapshots.

### Pixel conformance tests

Use tiny hand-computed fixtures for arithmetic and the full archive for historic
compatibility. Compare:

- dimensions and format;
- all RGBA bytes, including invisible RGB;
- maximum per-channel error;
- differing pixel/channel counts;
- first mismatch location;
- decoded-pixel hash;
- encoded bytes only when the contract is specifically byte identity or
  passthrough.

### Differential tests

While TypeScript remains, compile equivalent programs through old and new paths
and compare observable results. Do not require their internal graphs to match.

### Fuzz/property tests

Fuzz the lexer/parser, semantic compiler, serialized-module reader, graph
validator, collection arithmetic, and image dimension calculations. Useful
properties include deterministic compile output, no crashes on arbitrary source,
crop/canvas bounds safety, rotate-four-times identity, and alias stability.

### Cross-target tests

At minimum:

- native debug with sanitizers;
- native release;
- Wasm compile and browser smoke test;
- 32-bit compile where practical;
- big-endian serialization tests even before Wii hardware CI exists;
- GPU-vs-CPU comparison tests per declared tolerance.

## 10. Performance and resource rules

Correctness and clean boundaries come first, but these constraints should be
designed in from the beginning:

- No implicit full collection materialization.
- No implicit full reference-archive load.
- No unbounded recursion from source-controlled graphs.
- All width/height/stride/cardinality arithmetic is overflow-checked.
- Hosts can set memory, node, source-byte, diagnostic, import-depth, graph-depth,
  collection-cardinality, output-count, and render-time/cancellation limits.
- Immutable graphs may be shared safely once thread rules are defined.
- Caches are optional, bounded, and owned/configured by a context or host.
- Raster assets can remain encoded until an operation needs decoded pixels.
- Exact byte aliases can pass through without decode/re-encode.
- GPU resources stay on-device unless the caller asks for readback.
- Compile-time graph-shape changes are distinguished from uniform-only changes.

Create benchmarks only after behavior exists, but begin with:

- parse/check of the starter program;
- enumeration of all 4,312 outputs without render;
- one 64x64 gradient/composite;
- the 304-item palette x mask plan with a 16-item preview;
- full reference render/package with a cold and warm cache.

## 11. What must not happen

- Do not delete or reorganize the reference set before generating and locking its
  manifest.
- Do not mechanically translate Zod types into public C structs.
- Do not make JSON the execution engine.
- Do not move CodeMirror or DOM concepts into C.
- Do not put filesystem, PNG, Godot, WebGPU, WebGL, or GX types in core headers.
- Do not eagerly expand all products or render all notebook outputs.
- Do not call a result deterministic without specifying profile, operation, and
  equivalence class.
- Do not add archive-specific core operations when a compatibility module or
  raster source is more honest.
- Do not require a GPU to match the reference rasterizer byte-for-byte unless that
  operation/backend combination has a test proving it.
- Do not design the GPU backend before the semantic graph and CPU results are
  stable.
- Do not design dynamic materials by treating a static 4,312-image exporter as a
  shader compiler.
- Do not maintain two production BinScript compilers after the Wasm transition.
- Do not let a full-parity milestone prevent useful partial integrations with
  explicit raster fallback.

## 12. Definition of success

The rebuild has reached the `libbinblock` vision when all of the following are
true:

1. A portable C library accepts BinScript or direct C graph construction and
   produces the same typed semantic program.
2. The public ABI has no dependency on TypeScript, JSON, browser APIs, a filesystem,
   Godot, or a particular graphics API.
3. The canonical CPU backend defines and tests deterministic pixel semantics.
4. Large collections are lazy, bounded, sliceable, and rendered on demand.
5. The complete 4,312-image program is visible as BinScript plus explicit
   compatibility assets—not hidden as an opaque hard-coded stage catalogue.
6. The reference manifest and reports state exactly which outputs are analytic,
   aliases, raster-backed, exact, or approximate.
7. The browser notebook uses the C implementation through Wasm and loads reference
   data on demand.
8. At least one accelerated backend runs supported graph regions and falls back
   safely for the rest.
9. Godot consumes the library as an extension with importable resources,
   parameters, previews, and a texture/material path.
10. A Wii host can consume a bounded/precompiled program and render through GX or
    CPU-baked textures without changes to core semantics.
11. The TypeScript prototype remains valuable history and differential evidence,
    but it no longer defines production semantics.

The guiding principle for every change is:

> Put semantics in the portable graph and reference rasterizer; put policy and
> platform mechanisms in hosts and backends; keep the reference set as evidence.
