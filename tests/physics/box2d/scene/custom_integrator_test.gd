extends SceneTree

var callbacks := 0
var gravity := Vector2.ZERO
var damping := 0.0

func _initialize() -> void:
	call_deferred("run")

func on_state(state: PhysicsDirectBodyState2D) -> void:
	callbacks += 1
	gravity = state.total_gravity
	damping = state.total_linear_damp

func run() -> void:
	var space := PhysicsServer2D.space_create()
	PhysicsServer2D.space_set_active(space, true)
	var circle := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(circle, 10.0)
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_add_shape(body, circle)
	PhysicsServer2D.body_set_param(body, PhysicsServer2D.BODY_PARAM_GRAVITY_SCALE, 0.5)
	PhysicsServer2D.body_set_param(body, PhysicsServer2D.BODY_PARAM_LINEAR_DAMP, 3.0)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_CAN_SLEEP, false)
	PhysicsServer2D.body_set_omit_force_integration(body, true)
	PhysicsServer2D.body_set_space(body, space)
	PhysicsServer2D.body_set_force_integration_callback(body, on_state)
	PhysicsServer2D.body_set_constant_force(body, Vector2(0, 10000))
	PhysicsServer2D.body_apply_central_impulse(body, Vector2(60, 0))
	for tick in 30:
		await physics_frame
	var velocity: Vector2 = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY)
	var passed := callbacks > 20 and velocity.is_equal_approx(Vector2(60, 0)) and gravity.is_equal_approx(Vector2(0, 490)) and damping >= 3.0
	passed = passed and PhysicsServer2D.body_is_omitting_force_integration(body)
	PhysicsServer2D.body_set_omit_force_integration(body, false)
	PhysicsServer2D.body_apply_central_force(body, Vector2(0, 10000))
	PhysicsServer2D.body_set_omit_force_integration(body, true)
	var area := PhysicsServer2D.area_create()
	var rectangle := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(rectangle, Vector2(200, 200))
	PhysicsServer2D.area_add_shape(area, rectangle)
	PhysicsServer2D.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY_OVERRIDE_MODE, PhysicsServer2D.AREA_SPACE_OVERRIDE_REPLACE)
	PhysicsServer2D.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY, 400.0)
	PhysicsServer2D.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY_VECTOR, Vector2.UP)
	PhysicsServer2D.area_set_param(area, PhysicsServer2D.AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE, PhysicsServer2D.AREA_SPACE_OVERRIDE_REPLACE)
	PhysicsServer2D.area_set_param(area, PhysicsServer2D.AREA_PARAM_LINEAR_DAMP, 7.0)
	PhysicsServer2D.area_set_space(area, space)
	for tick in 10:
		await physics_frame
	velocity = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY)
	passed = passed and gravity.is_equal_approx(Vector2(0, -200)) and is_equal_approx(damping, 10.0) and velocity.is_equal_approx(Vector2(60, 0))
	PhysicsServer2D.free_rid(area)
	PhysicsServer2D.body_set_constant_force(body, Vector2.ZERO)
	PhysicsServer2D.body_set_omit_force_integration(body, false)
	for tick in 10:
		await physics_frame
	velocity = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY)
	passed = passed and velocity.y > 10 and velocity.x < 60 and not PhysicsServer2D.body_is_omitting_force_integration(body)
	for rid in [body, circle, rectangle, space]:
		PhysicsServer2D.free_rid(rid)
	print("RESULT: PASS - custom integration, scaled area getters and restored forces" if passed else "RESULT: FAIL - custom integration or environment getters")
	quit(0 if passed else 1)
