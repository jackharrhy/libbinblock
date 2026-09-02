export const pipelineStages = [
  {
    name: 'Parse',
    path: 'lib/frontend/syntax.c',
    symbol: 'bb_syntax_parse',
    description: 'Tokenizes bytes, keeps trivia, builds a recoverable syntax tree, and records byte-ranged diagnostics.',
  },
  {
    name: 'Lower',
    path: 'lib/semantic/program_evaluate.c',
    symbol: 'bb_program_compile_with_options',
    description: 'Resolves names and types, then lowers image expressions into immutable graph nodes and artifact collections.',
  },
  {
    name: 'Index',
    path: 'lib/core/collection.c',
    symbol: 'bb_collection_get',
    description: 'Walks a finite plan to obtain one artifact by index. Product and slice operations do not allocate every result.',
  },
  {
    name: 'Render',
    path: 'lib/raster/graph_render.c',
    symbol: 'bb_image_graph_render_raster_with_options',
    description: 'Marks the nodes required by one root, checks the pixel-work limit, and evaluates them into RGBA8 surfaces.',
  },
] as const;

export const targetDescriptions = [
  {
    name: 'Native C and CLI',
    path: 'cli/ + include/binblock/',
    description: 'The CLI handles files and PNG output. Other C hosts use the same program, collection, graph, and raster APIs.',
  },
  {
    name: 'Browser',
    path: 'bindings/wasm/ + bindings/javascript/',
    description:
      'A session table exposes generation-checked integer handles. JavaScript copies source, diagnostics, parameters, and pixels across the Wasm boundary.',
  },
  {
    name: 'Godot',
    path: 'integrations/godot/',
    description:
      'BinProgram recompiles editor parameters and copies a rendered RGBA8 surface into a PackedByteArray, Image, and ImageTexture.',
  },
  {
    name: 'Nintendo Wii',
    path: 'integrations/wii/',
    description:
      'The adapter renders on the CPU, pads dimensions to 4x4 tiles, writes GX RGBA8 AR and GB planes, and passes caller-owned memory to an upload callback.',
  },
] as const;

export const importantPaths = [
  ['include/binblock/program.h', 'Compile options, diagnostics, traces, parameters, output metadata, and rendering.'],
  ['include/binblock/graph.h', 'The 18 image-node kinds and the public graph query and construction API.'],
  ['include/binblock/collection.h', 'Values, artifacts, and the lazy collection plan API.'],
  ['lib/frontend/syntax.c', 'Lexer, parser, trivia retention, recovery, and syntax diagnostics.'],
  ['lib/semantic/program_compile.c', 'Imports, bindings, compile options, graph sealing, and output publication.'],
  ['lib/semantic/program_members.c', 'Lowering for image methods such as size, mask, over, crop, and tint.'],
  ['lib/core/graph.c', 'Node validation, structural hashes, deduplication, depth limits, and provenance.'],
  ['lib/core/collection.c', 'Reference-counted lazy plan nodes and indexed evaluation.'],
  ['lib/raster/graph_render.c', 'Dependency marking, work limits, cancellation, and CPU graph evaluation.'],
  ['bindings/wasm/wasm_api.c', 'Generation-checked sessions and copy-based browser ABI.'],
  ['integrations/godot/src/bin_program.cpp', 'Godot resources, inspector parameters, Image, and ImageTexture conversion.'],
  ['integrations/wii/binblock_wii.c', 'GX texture measurement, tiled encoding, and upload callback.'],
] as const;

export const graphNodeKinds = [
  'fill',
  'asset',
  'alpha field',
  'preset gradient',
  'linear gradient',
  'elliptical gradient',
  'crop',
  'canvas',
  'rotate',
  'opacity',
  'composite',
  'mask',
  'resize',
  'invert alpha',
  'set visible RGB',
  'tint chroma',
  'two-color remap',
  'RGB shift',
] as const;
