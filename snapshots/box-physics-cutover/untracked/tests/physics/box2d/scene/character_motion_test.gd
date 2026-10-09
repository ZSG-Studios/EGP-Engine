extends SceneTree

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var floor_body := StaticBody2D.new()
	var floor_collision := CollisionShape2D.new()
	var floor_shape := RectangleShape2D.new()
	floor_shape.size = Vector2(1000, 20)
	floor_collision.shape = floor_shape
	floor_body.add_child(floor_collision)
	floor_body.position = Vector2(0, 200)
	root.add_child(floor_body)
	var character := CharacterBody2D.new()
	var collision := CollisionShape2D.new()
	var capsule := CapsuleShape2D.new()
	capsule.radius = 10
	capsule.height = 40
	collision.shape = capsule
	character.add_child(collision)
	root.add_child(character)
	for tick in 90:
		await physics_frame
		character.velocity.y += 980.0 / 60.0
		character.move_and_slide()
	var passed := character.is_on_floor() and character.position.y > 160 and character.position.y < 180
	character.free()
	floor_body.free()
	print("RESULT: PASS - character falls and settles on floor" if passed else "RESULT: FAIL - character floor motion")
	quit(0 if passed else 1)
