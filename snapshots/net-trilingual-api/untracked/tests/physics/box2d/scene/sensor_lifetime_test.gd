extends SceneTree
var events: Array = []
var freed := false
var body: RID

func _initialize() -> void:
	call_deferred("run")

func on_monitor(status: int, other: RID, _id: int, other_shape: int, self_shape: int) -> void:
	events.append([status, other, other_shape, self_shape])
	if status == PhysicsServer2D.AREA_BODY_ADDED and not freed:
		freed = true
		PhysicsServer2D.free_rid(body)

func run() -> void:
	var space := PhysicsServer2D.space_create()
	PhysicsServer2D.space_set_active(space, true)
	var shape := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(shape, 20.0)
	var area := PhysicsServer2D.area_create()
	PhysicsServer2D.area_add_shape(area, shape)
	PhysicsServer2D.area_add_shape(area, shape, Transform2D(0, Vector2(5, 0)))
	PhysicsServer2D.area_set_space(area, space)
	PhysicsServer2D.area_set_monitor_callback(area, on_monitor)
	body = PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_set_space(body, space)
	for tick in 8:
		await physics_frame
	var added := 0
	var removed := 0
	var indices := {}
	for event in events:
		if event[0] == PhysicsServer2D.AREA_BODY_ADDED:
			added += 1
			indices[event[3]] = true
		else:
			removed += 1
	var passed := freed and added == 2 and removed == 2 and indices.has(0) and indices.has(1)
	# Trigger a shape-vector rebuild after the visitor was freed in a callback.
	PhysicsServer2D.area_remove_shape(area, 0)
	PhysicsServer2D.shape_set_data(shape, 30.0)
	PhysicsServer2D.free_rid(area)
	PhysicsServer2D.free_rid(space)
	PhysicsServer2D.free_rid(shape)
	print("RESULT: PASS - stable sensor indices and deletion during callbacks" if passed else "RESULT: FAIL - sensor lifetime: " + str(events))
	quit(0 if passed else 1)
