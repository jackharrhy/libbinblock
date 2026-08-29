extends CharacterBody2D

@export var speed := 190.0

@onready var sprite: Sprite2D = %PlayerSprite


func _physics_process(_delta: float) -> void:
	if Engine.is_editor_hint():
		return

	var direction := Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down")
	direction += Vector2(
		float(Input.is_key_pressed(KEY_D)) - float(Input.is_key_pressed(KEY_A)),
		float(Input.is_key_pressed(KEY_S)) - float(Input.is_key_pressed(KEY_W)),
	)
	velocity = direction.normalized() * speed if direction.length_squared() > 0.0 else Vector2.ZERO
	move_and_slide()

	if velocity.length_squared() > 0.0:
		sprite.rotation = sin(Time.get_ticks_msec() * 0.012) * 0.08
	else:
		sprite.rotation = lerpf(sprite.rotation, 0.0, 0.18)
