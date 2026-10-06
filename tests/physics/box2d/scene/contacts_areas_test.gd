extends SceneTree
var contact_seen := false
var finite_contact_velocity := true

func _initialize() -> void:
	call_deferred("run")

func on_state(state: PhysicsDirectBodyState2D) -> void:
	if state.get_contact_count() > 0:
		contact_seen = true
		finite_contact_velocity = finite_contact_velocity and state.get_contact_local_velocity_at_position(0).is_finite()

func run() -> void:
	var floor_body := StaticBody2D.new()
	var floor_collision := CollisionShape2D.new()
	var floor_shape := RectangleShape2D.new()
	floor_shape.size = Vector2(1000, 20)
	floor_collision.shape = floor_shape
	floor_body.add_child(floor_collision)
	floor_body.position = Vector2(0, 200)
	root.add_child(floor_body)
	var body := RigidBody2D.new()
	body.contact_monitor = true
	body.max_contacts_reported = 8
	var collision := CollisionShape2D.new()
	var circle := CircleShape2D.new()
	circle.radius = 10
	collision.shape = circle
	body.add_child(collision)
	root.add_child(body)
	PhysicsServer2D.body_set_force_integration_callback(body.get_rid(), on_state)
	for tick in 90:
		await physics_frame
	var passed := contact_seen and finite_contact_velocity and body.position.y > 175 and body.position.y < 185 and body.get_colliding_bodies().has(floor_body)
	var area := Area2D.new()
	area.gravity_space_override = Area2D.SPACE_OVERRIDE_REPLACE
	area.gravity = 400
	area.gravity_direction = Vector2.UP
	var area_collision := CollisionShape2D.new()
	var area_shape := RectangleShape2D.new()
	area_shape.size = Vector2(500, 500)
	area_collision.shape = area_shape
	area.add_child(area_collision)
	area.position = Vector2(0, 100)
	root.add_child(area)
	body.sleeping = false
	for tick in 45:
		await physics_frame
	passed = passed and body.linear_velocity.y < -10 and body.position.y < 175
	body.free()
	area.free()
	floor_body.free()
	print("RESULT: PASS - native contacts and area gravity override" if passed else "RESULT: FAIL - contacts or area override")
	quit(0 if passed else 1)
