extends SceneTree

func _initialize() -> void:
	var passed := PhysicsServer2D.get_class() == "Box2DPhysicsServer2D" and PhysicsServer3D.get_class() == "Box3DPhysicsServer3D"
	passed = passed and not ClassDB.class_exists("GodotPhysicsServer2D") and not ClassDB.class_exists("GodotPhysicsServer3D") and not ClassDB.class_exists("JoltPhysicsServer3D")
	for dimension in ["2d", "3d"]:
		for info in ProjectSettings.get_property_list():
			if info.name == "physics/" + dimension + "/physics_engine":
				passed = passed and info.hint_string == ("DEFAULT,Box2D Physics" if dimension == "2d" else "DEFAULT,Box3D Physics")
	print("RESULT: PASS - sole default Box2D and Box3D backends" if passed else "RESULT: FAIL - backend cutover")
	quit(0 if passed else 1)
