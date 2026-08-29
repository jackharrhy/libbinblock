# Godot integration

`binblock-godot` is an opt-in Godot 4 GDExtension consumer of the public C API.
It registers:

- `BinProgram`, an importable `.binscript`/`.bbm` resource with structured
  diagnostics, parameter properties, output metadata, and CPU-baked images or
  textures;
- `BinTexture`, a resource selecting one output and item from a `BinProgram`;
- a resource loader for source and versioned precompiled modules.

Build against an official `godot-cpp` checkout compatible with Godot 4.3 or
newer:

```sh
cmake -S . -B .build/godot -G Ninja \
  -DBB_BUILD_GODOT=ON \
  -DGODOT_CPP_ROOT=/path/to/godot-cpp
cmake --build .build/godot --target binblock-godot
```

Copy the resulting library into `integrations/godot/demo/bin/`, then open the
demo project and `world.tscn`. The main `@tool` scene imports `world.binscript`:
six Binblock images are assembled into an `ImageTexture` atlas consumed by a
real `TileMapLayer`, while a second output textures a movable
`CharacterBody2D`. Water and stone tiles carry physics polygons, and WASD or the
arrow keys move the actor through the world. The scene tree exposes the map,
actor, collision shape, and HUD as ordinary Godot nodes.

The world and gallery intentionally use Binblock's canonical harsh palette:
full-intensity primaries, their low-color partners, black/white/gray, and the
warm skin tones. Textures use nearest-neighbor sampling and the gallery keeps
each artifact at its authored pixel dimensions rather than stretching it to
fill a modern preview card.

`main.tscn` remains as a secondary editor gallery. It displays all 12 outputs
from `gallery.binscript`, exposes live `accent` and `tile-size` properties in the
Inspector, and refreshes when either changes. Godot objects remain entirely
inside this adapter; public BinBlock headers contain no Godot types.

On macOS, the checked-in demo can be opened directly with:

```sh
"/Applications/Godot 4.6.3.app/Contents/MacOS/Godot" \
  --editor \
  --path "$PWD/integrations/godot/demo"
```

The runtime gate accepts an official Godot 4.5 executable and the built extension:

```sh
cmake \
  -DGODOT_EXECUTABLE=/path/to/godot \
  -DGODOT_EXTENSION="$PWD/.build/godot/libbinblock_godot.dylib" \
  -DPROJECT_ROOT="$PWD" \
  -P tests/integration/godot_smoke.cmake
```

It loads the real GDExtension, imports the two-output BinScript resource, checks
its three-parameter schema, renders all six tiles and the actor, populates all
240 map cells, then drives the `CharacterBody2D` into the generated water tiles
and requires a real slide collision.
