@tool
extends Control

const ITEM_NAMES := [
	"RED HI",
	"BLUE RADIAL",
	"CYAN CORE",
	"GREEN DIAGONAL",
	"YELLOW / RED",
	"MAGENTA COLUMN",
	"SKIN RADIAL",
	"SKIN BANDS",
	"MONO BARS",
	"CYAN / RED",
	"LOW SPECTRUM",
	"RED MASK",
]

@export_category("Binblock Gallery")
@export var program: BinProgram:
	set(value):
		_disconnect_program()
		program = value
		_connect_program()
		_queue_rebuild()
@export var animate_previews := true

@onready var grid: GridContainer = %GalleryGrid
@onready var status: Label = %Status

var _previews: Array[TextureRect] = []
var _elapsed := 0.0
var _rebuild_queued := false
var _headless_override_applied := false


func _ready() -> void:
	_connect_program()
	_queue_rebuild()
	set_process(not Engine.is_editor_hint() and animate_previews)


func _process(delta: float) -> void:
	_elapsed += delta
	for index in _previews.size():
		var alpha := 0.94 + sin(_elapsed * 1.35 + index * 0.72) * 0.06
		_previews[index].modulate = Color(1.0, 1.0, 1.0, alpha)


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
	call_deferred("_rebuild_gallery")


func _rebuild_gallery() -> void:
	_rebuild_queued = false
	_previews.clear()
	for child in grid.get_children():
		grid.remove_child(child)
		child.queue_free()

	if program == null:
		_fail("Assign gallery.binscript to the Gallery Program property")
		return
	if program.has_errors():
		_fail("BinScript diagnostics: %s" % program.get_diagnostics())
		return
	if program.get_output_count() != 1:
		_fail("Expected one gallery output")
		return
	if not _has_expected_inspector_schema():
		_fail("Expected diagnostics plus accent and tile-size Inspector properties")
		return
	if DisplayServer.get_name() == "headless" and not _headless_override_applied:
		_headless_override_applied = true
		if not program.set_parameter("tile-size", 60):
			_fail("BinProgram rejected its tile-size parameter")
		return

	var output := program.get_output_info(0)
	var cardinality := int(output.get("cardinality", 0))
	if cardinality != ITEM_NAMES.size():
		_fail("Expected %d gallery items, got %d" % [ITEM_NAMES.size(), cardinality])
		return

	for item_index in cardinality:
		var texture := program.render_texture(0, item_index)
		if texture == null:
			_fail("Could not render gallery item %d" % item_index)
			return
		grid.add_child(_make_card(item_index, texture))

	status.text = "%d OUTPUTS  •  CPU-BAKED RGBA8  •  LIVE GDEXTENSION" % cardinality
	status.modulate = Color("7de8c5")
	if DisplayServer.get_name() == "headless":
		print("BINBLOCK_GODOT_DEMO_OK outputs=1 cardinality=12 textures=12 parameters=2 tile-size=60")
		get_tree().quit(0)


func _has_expected_inspector_schema() -> bool:
	var parameters := {}
	for parameter in program.get_parameter_schema():
		parameters[String(parameter.get("name", ""))] = parameter.get("value")
	if parameters.size() != 2 or not parameters.has("accent") or not parameters.has("tile-size"):
		return false
	if _headless_override_applied and int(parameters["tile-size"]) != 60:
		return false

	var properties := {}
	for property in program.get_property_list():
		properties[String(property.name)] = true
	return (
		properties.has("diagnostics")
		and properties.has("parameters/accent")
		and properties.has("parameters/tile-size")
	)


func _make_card(item_index: int, texture: Texture2D) -> PanelContainer:
	var card := PanelContainer.new()
	card.custom_minimum_size = Vector2(236, 154)
	card.add_theme_stylebox_override("panel", _card_style())

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 10)
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_right", 10)
	margin.add_theme_constant_override("margin_bottom", 9)
	card.add_child(margin)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 7)
	margin.add_child(column)

	var preview_shell := PanelContainer.new()
	preview_shell.custom_minimum_size = Vector2(216, 96)
	preview_shell.add_theme_stylebox_override("panel", _preview_style())
	column.add_child(preview_shell)

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	preview_shell.add_child(center)

	var preview := TextureRect.new()
	preview.texture = texture
	preview.custom_minimum_size = texture.get_size()
	preview.expand_mode = TextureRect.EXPAND_KEEP_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	preview.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	preview.mouse_filter = Control.MOUSE_FILTER_IGNORE
	center.add_child(preview)
	_previews.push_back(preview)

	var metadata := HBoxContainer.new()
	column.add_child(metadata)

	var name_label := Label.new()
	name_label.text = ITEM_NAMES[item_index]
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_label.add_theme_color_override("font_color", Color("e8eefc"))
	name_label.add_theme_font_size_override("font_size", 12)
	metadata.add_child(name_label)

	var size_label := Label.new()
	size_label.text = "%d×%d" % [texture.get_width(), texture.get_height()]
	size_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	size_label.add_theme_color_override("font_color", Color("71809d"))
	size_label.add_theme_font_size_override("font_size", 11)
	metadata.add_child(size_label)
	return card


func _card_style() -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color("10192a")
	style.border_color = Color("22304a")
	style.set_border_width_all(1)
	style.set_corner_radius_all(12)
	return style


func _preview_style() -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color("080e1a")
	style.set_corner_radius_all(7)
	return style


func _fail(message: String) -> void:
	status.text = message
	status.modulate = Color("ff7189")
	push_error(message)
	if DisplayServer.get_name() == "headless":
		get_tree().quit(1)
