@tool
extends Node2D

const MAP_SIZE := Vector2i(20, 12)
const TILE_COUNT := 6
const TILE_MEADOW := 0
const TILE_PATH := 1
const TILE_WATER := 2
const TILE_BLOOM := 3
const TILE_STONE := 4
const TILE_MOSS := 5

@export_category("Binblock World")
@export var program: BinProgram:
	set(value):
		_disconnect_program()
		program = value
		_connect_program()
		_queue_rebuild()

@onready var tile_map: TileMapLayer = %WorldTiles
@onready var player: CharacterBody2D = %Player
@onready var player_sprite: Sprite2D = %PlayerSprite
@onready var player_collision: CollisionShape2D = %PlayerCollision
@onready var status: Label = %Status

var _rebuild_queued := false
var _headless_override_applied := false
var _placed_player := false


func _ready() -> void:
	_connect_program()
	_queue_rebuild()


func _connect_program() -> void:
	if program != null and not program.changed.is_connected(_on_program_changed):
		program.changed.connect(_on_program_changed)


func _disconnect_program() -> void:
	if program != null and program.changed.is_connected(_on_program_changed):
		program.changed.disconnect(_on_program_changed)


func _on_program_changed() -> void:
	_queue_rebuild()


func _queue_rebuild() -> void:
	if not is_node_ready() or _rebuild_queued:
		return
	_rebuild_queued = true
	call_deferred("_rebuild_world")


func _rebuild_world() -> void:
	_rebuild_queued = false
	if not _validate_program():
		return

	if DisplayServer.get_name() == "headless" and not _headless_override_applied:
		_headless_override_applied = true
		if not program.set_parameter("actor-color", Color("67e8f9")):
			_fail("BinProgram rejected the actor-color override")
		return

	var tile_images: Array[Image] = []
	var tile_size := 0
	for item_index in TILE_COUNT:
		var image := program.render_image(0, item_index)
		if image == null or image.get_width() != image.get_height():
			_fail("Tile %d did not render as a square image" % item_index)
			return
		if tile_size == 0:
			tile_size = image.get_width()
		elif image.get_width() != tile_size:
			_fail("All Binblock tiles must share one atlas cell size")
			return
		tile_images.push_back(image)

	var source_id := _build_tile_set(tile_images, tile_size)
	_paint_world(source_id)
	if not _render_player():
		return
	if not _placed_player:
		player.position = tile_map.position + tile_map.map_to_local(Vector2i(3, 4))
		_placed_player = true

	status.text = "2 OUTPUTS  •  6 BINBLOCK TILES  •  1 BINBLOCK ACTOR  •  LIVE COLLISIONS"
	status.modulate = Color("7de8c5")
	if DisplayServer.get_name() == "headless":
		if tile_map.get_used_cells().size() != MAP_SIZE.x * MAP_SIZE.y:
			_fail("TileMapLayer did not receive the complete world")
			return
		call_deferred("_run_headless_collision_smoke")


func _validate_program() -> bool:
	if program == null:
		_fail("Assign world.binscript to the World Program property")
		return false
	if program.has_errors():
		_fail("BinScript diagnostics: %s" % program.get_diagnostics())
		return false
	if program.get_output_count() != 2:
		_fail("Expected tile and actor outputs")
		return false
	if int(program.get_output_info(0).get("cardinality", 0)) != TILE_COUNT:
		_fail("Expected six tile artifacts")
		return false
	if int(program.get_output_info(1).get("cardinality", 0)) != 1:
		_fail("Expected one actor artifact")
		return false

	var parameter_names := {}
	for parameter in program.get_parameter_schema():
		parameter_names[String(parameter.get("name", ""))] = true
	if (
		parameter_names.size() != 3
		or not parameter_names.has("tile-size")
		or not parameter_names.has("actor-size")
		or not parameter_names.has("actor-color")
	):
		_fail("Expected tile-size, actor-size, and actor-color parameters")
		return false
	return true


func _build_tile_set(tile_images: Array[Image], tile_size: int) -> int:
	var atlas_image := Image.create(tile_size * TILE_COUNT, tile_size, false, Image.FORMAT_RGBA8)
	for item_index in TILE_COUNT:
		atlas_image.blit_rect(
			tile_images[item_index],
			Rect2i(Vector2i.ZERO, Vector2i(tile_size, tile_size)),
			Vector2i(item_index * tile_size, 0),
		)

	var atlas_source := TileSetAtlasSource.new()
	atlas_source.texture = ImageTexture.create_from_image(atlas_image)
	atlas_source.texture_region_size = Vector2i(tile_size, tile_size)
	for item_index in TILE_COUNT:
		atlas_source.create_tile(Vector2i(item_index, 0))

	var tile_set := TileSet.new()
	tile_set.tile_size = Vector2i(tile_size, tile_size)
	tile_set.add_physics_layer()
	tile_set.set_physics_layer_collision_layer(0, 1)
	tile_set.set_physics_layer_collision_mask(0, 1)
	var source_id := tile_set.add_source(atlas_source)
	_add_full_tile_collision(atlas_source, TILE_WATER, tile_size)
	_add_full_tile_collision(atlas_source, TILE_STONE, tile_size)
	tile_map.tile_set = tile_set
	tile_map.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	return source_id


func _add_full_tile_collision(source: TileSetAtlasSource, tile_index: int, tile_size: int) -> void:
	var tile_data := source.get_tile_data(Vector2i(tile_index, 0), 0)
	var half := tile_size * 0.5
	tile_data.add_collision_polygon(0)
	tile_data.set_collision_polygon_points(
		0,
		0,
		PackedVector2Array([
			Vector2(-half, -half),
			Vector2(half, -half),
			Vector2(half, half),
			Vector2(-half, half),
		]),
	)


func _paint_world(source_id: int) -> void:
	tile_map.clear()
	var stones := {
		Vector2i(6, 3): true,
		Vector2i(9, 6): true,
		Vector2i(16, 8): true,
		Vector2i(5, 9): true,
	}
	for y in MAP_SIZE.y:
		for x in MAP_SIZE.x:
			var coordinates := Vector2i(x, y)
			var tile_index := TILE_MEADOW
			if x == 0 or y == 0 or x == MAP_SIZE.x - 1 or y == MAP_SIZE.y - 1:
				tile_index = TILE_WATER
			elif x >= 12 and x <= 15 and y >= 3 and y <= 5:
				tile_index = TILE_WATER
			elif stones.has(coordinates):
				tile_index = TILE_STONE
			elif y == 8 and x >= 2 and x <= 17:
				tile_index = TILE_PATH
			elif (x * 7 + y * 11) % 17 == 0:
				tile_index = TILE_BLOOM
			elif (x * 13 + y * 5) % 19 == 0:
				tile_index = TILE_MOSS
			tile_map.set_cell(coordinates, source_id, Vector2i(tile_index, 0), 0)
	tile_map.update_internals()


func _render_player() -> bool:
	var texture := program.render_texture(1, 0)
	if texture == null:
		_fail("Could not render the Binblock actor")
		return false
	player_sprite.texture = texture
	var shape := player_collision.shape as CircleShape2D
	shape.radius = maxf(10.0, texture.get_width() * 0.34)
	return true


func _run_headless_collision_smoke() -> void:
	player.set_physics_process(false)
	await get_tree().physics_frame
	var collided := false
	for _step in 90:
		player.velocity = Vector2.LEFT * 420.0
		player.move_and_slide()
		if player.get_slide_collision_count() > 0:
			collided = true
			break
		await get_tree().physics_frame
	if not collided:
		_fail("The CharacterBody2D escaped the TileMapLayer water collision")
		return
	print("BINBLOCK_GODOT_WORLD_OK outputs=2 tiles=6 cells=240 actor=40x40 parameters=3 collision=water")
	get_tree().quit(0)


func _fail(message: String) -> void:
	status.text = message
	status.modulate = Color("ff7189")
	push_error(message)
	if DisplayServer.get_name() == "headless":
		get_tree().quit(1)
