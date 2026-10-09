extends SceneTree

func _initialize() -> void:
	for name in ["UAL2_Standard", "Mannequin_F"]:
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		var error := document.append_from_file("res://assets/ual2/"+name+".glb",state)
		if error != OK:
			push_error("GLTF load "+str(error))
			quit(1)
			return
		var model := document.generate_scene(state)
		root.add_child(model)
		var skeleton := model.find_child("*",true,false)
		_print(model,model)
		model.free()
	quit()

func _print(node: Node, model: Node) -> void:
	if node is AnimationPlayer:
		print("ANIMATOR path=",model.get_path_to(node)," root=",node.root_node," clips=",node.get_animation_list())
		var clip: Animation = node.get_animation("Walk_Carry_Loop")
		for i in range(mini(clip.get_track_count(),3)):
			print("TRACK ",clip.track_get_path(i))
	elif node is Skeleton3D:
		print("SKELETON path=",model.get_path_to(node)," bones=",node.get_bone_count())
	elif node is MeshInstance3D:
		print("MESH path=",model.get_path_to(node)," skeleton=",node.skeleton," aabb=",node.get_aabb())
	for child in node.get_children():
		_print(child,model)
