extends SceneTree

class ControlledBody extends RigidBody2D:
	var callback_count := 0
	func _integrate_forces(state: PhysicsDirectBodyState2D) -> void:
		callback_count += 1
		state.linear_velocity = Vector2(60, 0)

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var body := ControlledBody.new()
	body.custom_integrator = true
	body.can_sleep = false
	var collision := CollisionShape2D.new()
	var shape := CircleShape2D.new()
	shape.radius = 5
	collision.shape = shape
	body.add_child(collision)
	root.add_child(body)
	for frame in 30:
		await physics_frame
	var passed := body.callback_count > 20 and body.position.x > 20 and absf(body.position.y) < 0.01
	passed = passed and body.linear_velocity.is_equal_approx(Vector2(60, 0))
	body.free()
	print("RESULT: PASS - scene virtual integration controls native body" if passed else "RESULT: FAIL - scene integration callback")
	quit(0 if passed else 1)
