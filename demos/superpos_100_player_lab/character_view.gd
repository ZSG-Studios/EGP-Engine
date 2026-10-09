extends Node3D

const REQUIRED := ["Idle_Loop","Walk_Loop","Jog_Fwd_Loop","Sprint_Loop","Jump_Start","Jump_Loop","Jump_Land","Idle_FoldArms_Loop","Walk_Carry_Loop","OverhandThrow"]
static var male_scene: PackedScene
static var female_scene: PackedScene
static var shared_library: AnimationLibrary
var animator: AnimationPlayer
var skeleton: Skeleton3D
var current := ""
var state_time := 0.0
var grounded_before := true
var pulse_before := false
var action_remaining := 0.0
var landing_remaining := 0.0
var motion_velocity := Vector3.ZERO
var motion_grounded := true
var motion_pulse := false
var female := false

static func prepare() -> bool:
	if male_scene != null:
		return true
	var male := _load_model("UAL2_Standard")
	if male == null:
		return false
	var player := male.get_node("AnimationPlayer") as AnimationPlayer
	var pro := _load_model("UAL1_Pro")
	if pro==null:
		male.free()
		return false
	var pro_player := pro.get_node("AnimationPlayer") as AnimationPlayer
	shared_library=AnimationLibrary.new()
	for library in [pro_player.get_animation_library(""),player.get_animation_library("")]:
		for name in library.get_animation_list():
			if not shared_library.has_animation(name):
				var animation: Animation = library.get_animation(name)
				animation.loop_mode=Animation.LOOP_LINEAR if name.ends_with("_Loop") else Animation.LOOP_NONE
				shared_library.add_animation(name,animation)
	pro.free()
	player.remove_animation_library("")
	player.add_animation_library("",shared_library)
	for name in REQUIRED:
		if not player.has_animation(name):
			push_error("UAL2 missing required clip: "+name)
			male.free()
			return false
		var animation := player.get_animation(name)
		animation.loop_mode=Animation.LOOP_LINEAR if name.ends_with("_Loop") else Animation.LOOP_NONE
	male_scene=PackedScene.new()
	var result := male_scene.pack(male)
	male.free()
	if result != OK:
		return false
	var female_model := _load_model("Mannequin_F")
	if female_model == null:
		return false
	var rig := female_model.get_node("Armature/Skeleton3D") as Skeleton3D
	# Vendor mannequins share the rig. Validate every animation's target bone.
	for name in shared_library.get_animation_list():
		var animation := shared_library.get_animation(name)
		for track in range(animation.get_track_count()):
			var path := animation.track_get_path(track)
			if path.get_subname_count()>0 and rig.find_bone(path.get_subname(0))<0:
				push_error("Female UAL2 rig lacks animation target: "+str(path))
				female_model.free()
				return false
	var female_player := AnimationPlayer.new()
	female_player.name="AnimationPlayer"
	female_player.add_animation_library("",shared_library)
	female_model.add_child(female_player)
	female_player.owner=female_model
	female_scene=PackedScene.new()
	result=female_scene.pack(female_model)
	female_model.free()
	return result==OK

static func _load_model(name: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file("res://assets/ual2/"+name+".glb",state)!=OK:
		return null
	return document.generate_scene(state) as Node3D

func configure(color: Color, use_female: bool) -> bool:
	if not prepare():
		return false
	female=use_female
	if animator!=null:
		var previous_model := animator.get_parent()
		remove_child(previous_model)
		previous_model.queue_free()
	var model := (female_scene if female else male_scene).instantiate() as Node3D
	add_child(model)
	model.position.y=-0.9
	model.scale=Vector3.ONE*(1.8/(1.80821 if female else 1.82918))
	animator=model.get_node("AnimationPlayer") as AnimationPlayer
	skeleton=model.get_node("Armature/Skeleton3D") as Skeleton3D
	for child in skeleton.get_children():
		if child is MeshInstance3D:
			for surface in range(child.mesh.get_surface_count()):
				var material := StandardMaterial3D.new()
				material.albedo_color=color if surface==0 else Color("24384c")
				material.metallic=0.28 if surface==0 else 0.08
				material.roughness=0.48
				child.set_surface_override_material(surface,material)
	animator.play("Idle_Loop")
	animator.advance(0)
	animator.active=false
	current="Idle_Loop"
	return true

func present(velocity: Vector3, flags: int) -> void:
	motion_velocity=velocity
	motion_grounded=(flags&1)!=0
	motion_pulse=(flags&2)!=0
	animator.active=true

func _process(delta: float) -> void:
	if animator==null:
		return
	if not is_visible_in_tree():
		animator.active=false
		return
	state_time+=delta
	action_remaining=maxf(0,action_remaining-delta)
	landing_remaining=maxf(0,landing_remaining-delta)
	if motion_pulse and not pulse_before:
		action_remaining=1.2
	if motion_grounded and not grounded_before:
		landing_remaining=0.35
	if not motion_grounded and grounded_before:
		state_time=0
	var horizontal := Vector3(motion_velocity.x,0,motion_velocity.z)
	if horizontal.length_squared()>0.1:
		rotation.y=lerp_angle(rotation.y,atan2(horizontal.x,horizontal.z),1-exp(-delta*12))
	var speed := horizontal.length()
	var next := "Idle_Loop"
	if not motion_grounded:
		next="Jump_Start" if state_time<0.4 else "Jump_Loop"
	elif action_remaining>0:
		next="OverhandThrow"
	elif landing_remaining>0:
		next="Jump_Land"
	elif horizontal.length_squared()>0.09:
		next="Sprint_Loop" if speed>6.5 else "Jog_Fwd_Loop" if speed>3.2 else "Walk_Loop" if speed>1.5 else "Walk_Carry_Loop"
	if current!=next:
		animator.play(next,0.16)
		current=next
	var gait_speed := 7.0 if next=="Sprint_Loop" else 4.5 if next=="Jog_Fwd_Loop" else 2.0
	animator.speed_scale=clampf(speed/gait_speed,0.65,1.6) if next in ["Walk_Carry_Loop","Walk_Loop","Jog_Fwd_Loop","Sprint_Loop"] else 1.0
	grounded_before=motion_grounded
	pulse_before=motion_pulse
