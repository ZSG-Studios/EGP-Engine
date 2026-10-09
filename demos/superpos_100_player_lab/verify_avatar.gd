extends SceneTree

const VIEW := preload("res://character_view.gd")
var failed := false

func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	if not VIEW.prepare():
		push_error("Animation libraries or rig validation failed")
		quit(1)
		return
	var stage := Node3D.new()
	root.add_child(stage)
	var environment := WorldEnvironment.new()
	environment.environment=Environment.new()
	environment.environment.background_mode=Environment.BG_COLOR
	environment.environment.background_color=Color("101e30")
	environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color=Color.WHITE
	environment.environment.ambient_light_energy=0.6
	stage.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees=Vector3(-50,-25,0)
	stage.add_child(light)
	var camera := Camera3D.new()
	camera.projection=Camera3D.PROJECTION_ORTHOGONAL
	camera.size=21
	stage.add_child(camera)
	camera.position=Vector3(0,7,14)
	camera.look_at(Vector3(0,1,0))
	var clips := ["Idle_Loop","Walk_Loop","Jog_Fwd_Loop","Sprint_Loop","Jump_Loop","OverhandThrow","Walk_Carry_Loop"]
	var results: Array = []
	for row in range(2):
		for column in range(clips.size()):
			var avatar: Node3D=VIEW.new()
			stage.add_child(avatar)
			if not avatar.configure(Color("62d6ef") if row==0 else Color("c2a1ff"),row==1):
				failed=true
				continue
			avatar.position=Vector3((column-3)*2.8,0.9,(row-0.5)*4.0)
			avatar.set_process(false)
			var rig: Skeleton3D=avatar.skeleton
			var before: Array = []
			for bone in range(rig.get_bone_count()):
				before.append(rig.get_bone_pose(bone))
			avatar.animator.active=true
			avatar.animator.play(clips[column],0)
			avatar.animator.advance(0.6)
			avatar.animator.speed_scale=0
			rig.force_update_all_bone_transforms()
			var changed := 0
			for bone in range(rig.get_bone_count()):
				if not before[bone].is_equal_approx(rig.get_bone_pose(bone)):
					changed+=1
			if changed<3:
				push_error("Insufficient real skeletal motion: "+clips[column]+" female="+str(row==1))
				failed=true
			results.append({"female":row==1,"clip":clips[column],"changed_bones":changed})
			var label := Label3D.new()
			label.text=clips[column].trim_suffix("_Loop")
			label.font_size=28
			label.billboard=BaseMaterial3D.BILLBOARD_ENABLED
			label.position=Vector3(0,2.2,0)
			avatar.add_child(label)
	var out := ProjectSettings.globalize_path("res://../../.build/diagnostics/superpos-100/")
	if DisplayServer.get_name()!="headless":
		await process_frame
		await RenderingServer.frame_post_draw
		if root.get_texture().get_image().save_png(out+"animation-gallery.png")!=OK:
			failed=true
	var file := FileAccess.open(out+"animation-receipt.json",FileAccess.WRITE)
	file.store_string(JSON.stringify({"passed":not failed,"unique_clips":VIEW.shared_library.get_animation_list().size(),"bones":65,"poses":results},"\t"))
	file.close()
	print("AVATAR_QUALIFICATION ","FAIL" if failed else "PASS"," clips=",VIEW.shared_library.get_animation_list().size()," posed_mannequins=",results.size())
	quit(1 if failed else 0)
