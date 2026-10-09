extends SceneTree
func _initialize() -> void:
	print("SCENE_BACKEND ",PhysicsServer3D.get_class()," soft_body=",ClassDB.class_exists("SoftBody3D")," blend_space=",ClassDB.class_exists("AnimationNodeBlendSpace2D"))
	quit()
