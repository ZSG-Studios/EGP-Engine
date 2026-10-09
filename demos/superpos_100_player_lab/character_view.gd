extends Node3D

const REQUIRED := ["Idle_Loop","Walk_Loop","Jog_Fwd_Loop","Sprint_Loop","Jump_Start","Jump_Loop","Jump_Land","Idle_FoldArms_Loop","Walk_Carry_Loop","OverhandThrow"]
const JOG := ["Jog_Fwd_Loop","Jog_Fwd_R_Loop","Jog_Right_Loop","Jog_Bwd_R_Loop","Jog_Bwd_Loop","Jog_Bwd_L_Loop","Jog_Left_Loop","Jog_Fwd_L_Loop"]
const CROUCH := ["Crouch_Fwd_Loop","Crouch_Fwd_R_Loop","Crouch_Right_Loop","Crouch_Bwd_R_Loop","Crouch_Bwd_Loop","Crouch_Bwd_L_Loop","Crouch_Left_Loop","Crouch_Fwd_L_Loop"]
const POSES := ["Idle_Loop","Jump_Start","Jump_Loop","Jump_Land","OverhandThrow","Push_Enter","Push_Loop","Push_Exit","Crawl_Enter","Crawl_Exit","Crouch_Enter","Crouch_Exit","Swim_Idle_Loop","Swim_Fwd_Loop","GroundSit_Enter","GroundSit_Idle_Loop","GroundSit_Exit","Sitting_Enter","Sitting_Idle_Loop","Sitting_Exit","Death01","Death02","Hit_Chest","Interact"]
static var male_scene: PackedScene
static var female_scene: PackedScene
static var shared_library: AnimationLibrary
static var shared_tree: AnimationNodeBlendTree
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
var tree: AnimationTree
var playback: AnimationNodeStateMachinePlayback
var motion_flags := 1
var facing := 0.0
var stance_before := 0
var transition_remaining := 0.0
var transition_clip := ""
var dead_before := false
var death_clip := "Death01"


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
	for name in REQUIRED+JOG+CROUCH+POSES+["Crawl_Fwd_Loop","Crawl_Bwd_Loop","Crawl_Left_Loop","Crawl_Right_Loop","Crawl_Idle_Loop"]:
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
	if tree!=null:
		remove_child(tree)
		tree.queue_free()
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

static func _space(clips: Array, idle: String) -> AnimationNodeBlendSpace2D:
	var space := AnimationNodeBlendSpace2D.new()
	space.auto_triangles=false
	# EGP cyclic synchronization keeps different directional clip lengths in phase.
	space.set_sync_mode(AnimationNodeBlendSpace2D.SYNC_MODE_CYCLIC_MUTABLE)
	var center := AnimationNodeAnimation.new()
	center.animation=idle
	space.add_blend_point(center,Vector2.ZERO,-1,"idle")
	for i in range(clips.size()):
		var node := AnimationNodeAnimation.new()
		node.animation=clips[i]
		var angle := TAU*float(i)/clips.size()
		space.add_blend_point(node,Vector2(sin(angle),-cos(angle)),-1,"direction_"+str(i))
	for i in range(clips.size()):
		space.add_triangle(0,i+1,(i+1)%clips.size()+1)
	return space

static func _build_shared_tree() -> AnimationNodeBlendTree:
	var machine := AnimationNodeStateMachine.new()
	var walk := JOG.duplicate()
	walk[0]="Walk_Loop"
	var sprint := JOG.duplicate()
	sprint[0]="Sprint_Loop"
	machine.add_node("walk",_space(walk,"Idle_Loop"))
	machine.add_node("jog",_space(JOG,"Idle_Loop"))
	machine.add_node("sprint",_space(sprint,"Idle_Loop"))
	machine.add_node("crouch",_space(CROUCH,"Crouch_Idle_Loop"))
	# Four authored crawl axes blend continuously to cover the diagonal directions.
	machine.add_node("crawl",_space(["Crawl_Fwd_Loop","Crawl_Right_Loop","Crawl_Bwd_Loop","Crawl_Left_Loop"],"Crawl_Idle_Loop"))
	for clip in POSES:
		var node := AnimationNodeAnimation.new()
		node.animation=clip
		machine.add_node(clip,node)
	var names := ["walk","jog","sprint","crouch","crawl"]+POSES
	for a in names:
		for b in names:
			if a==b:
				continue
			var transition := AnimationNodeStateMachineTransition.new()
			transition.xfade_time=0.15
			machine.add_transition(a,b,transition)
	var root_tree := AnimationNodeBlendTree.new()
	root_tree.add_node("States",machine)
	root_tree.add_node("Rate",AnimationNodeTimeScale.new())
	root_tree.connect_node("Rate",0,"States")
	root_tree.connect_node("output",0,"Rate")
	return root_tree

func _setup_tree() -> void:
	if shared_tree==null:
		shared_tree=_build_shared_tree()
	tree=AnimationTree.new()
	add_child(tree)
	tree.anim_player=tree.get_path_to(animator)
	tree.tree_root=shared_tree
	tree.callback_mode_process=AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	tree.active=true
	playback=tree.get("parameters/States/playback")
	playback.start("Idle_Loop")
	current="Idle_Loop"

func present(velocity: Vector3, flags: int, heading: float = NAN) -> void:
	motion_velocity=velocity
	motion_flags=flags
	if animator!=null:
		animator.get_parent().position.y=-0.4 if flags&16 else -0.6 if flags&8 else -0.9
	motion_grounded=(flags&1)!=0
	motion_pulse=(flags&2)!=0
	if is_finite(heading):
		facing=heading
	elif velocity.length_squared()>0.1:
		facing=atan2(velocity.x,velocity.z)

# Animation LOD: the lab sets how many frames pass between pose evaluations from camera
# distance and visibility. Skipped frames accumulate time, so motion stays in phase.
const BLEND_PARAMETERS := {"walk":&"parameters/States/walk/blend_position","jog":&"parameters/States/jog/blend_position","sprint":&"parameters/States/sprint/blend_position","crouch":&"parameters/States/crouch/blend_position","crawl":&"parameters/States/crawl/blend_position"}
const RATE_PARAMETER := &"parameters/Rate/scale"
var last_blend_state := ""
var last_blend := Vector2(INF,INF)
var last_rate := -1.0
var lod := 1
# Frame profiling shared by every character view.
static var animate_usec := 0
static var animated := 0
var lod_phase := 0
var lod_delta := 0.0


func _process(delta: float) -> void:
	lod_delta+=delta
	lod_phase+=1
	if lod>1 and lod_phase%lod!=0:
		rotation.y=lerp_angle(rotation.y,facing,1-exp(-delta*12))
		return
	var step := minf(lod_delta,0.25)
	lod_delta=0.0
	var started := Time.get_ticks_usec()
	_animate(step)
	animate_usec+=Time.get_ticks_usec()-started
	animated+=1


func _animate(delta: float) -> void:
	if animator==null or not is_visible_in_tree():
		return
	if tree==null:
		_setup_tree()
	state_time+=delta
	action_remaining=maxf(0,action_remaining-delta)
	landing_remaining=maxf(0,landing_remaining-delta)
	transition_remaining=maxf(0,transition_remaining-delta)
	if motion_pulse and not pulse_before:
		action_remaining=1.2
	if motion_grounded and not grounded_before:
		landing_remaining=0.35
	if not motion_grounded and grounded_before:
		state_time=0
	var stance := motion_flags & (8|16|32|64|128|256)
	var dead := (stance&256)!=0
	if dead and not dead_before:
		death_clip="Death02" if female else "Death01"
	if stance!=stance_before and not dead:
		transition_clip=""
		if stance_before&128:
			transition_clip="Sitting_Exit"
		elif stance_before&64:
			transition_clip="GroundSit_Exit"
		elif stance_before&32:
			transition_clip="Push_Exit"
		elif stance_before&16:
			transition_clip="Crawl_Exit"
		elif stance_before&8:
			transition_clip="Crouch_Exit"
		elif stance&128:
			transition_clip="Sitting_Enter"
		elif stance&64:
			transition_clip="GroundSit_Enter"
		elif stance&32:
			transition_clip="Push_Enter"
		elif stance&16:
			transition_clip="Crawl_Enter"
		elif stance&8:
			transition_clip="Crouch_Enter"
		if not transition_clip.is_empty():
			transition_remaining=minf(0.85,animator.get_animation(transition_clip).length)
	var horizontal := Vector3(motion_velocity.x,0,motion_velocity.z)
	rotation.y=lerp_angle(rotation.y,facing,1-exp(-delta*12))
	var speed := horizontal.length()
	var next := "Idle_Loop"
	if dead:
		next=death_clip
	elif transition_remaining>0:
		next=transition_clip
	elif stance&128:
		next="Sitting_Idle_Loop"
	elif stance&64:
		next="GroundSit_Idle_Loop"
	elif stance&32:
		next="Push_Loop"
	elif motion_flags&512:
		next="Swim_Fwd_Loop" if speed>0.2 else "Swim_Idle_Loop"
	elif stance&16:
		next="crawl"
	elif stance&8:
		next="crouch"
	elif not motion_grounded:
		next="Jump_Start" if state_time<0.4 else "Jump_Loop"
	elif action_remaining>0:
		next="OverhandThrow"
	elif landing_remaining>0:
		next="Jump_Land"
	elif speed>0.3:
		next="sprint" if speed>6.5 else "jog" if speed>3.2 else "walk"
	if current!=next:
		playback.travel(next)
		current=next
	var local := horizontal.rotated(Vector3.UP,-facing)
	var blend := Vector2(local.x,-local.z).normalized() if speed>0.2 else Vector2.ZERO
	# Only the active locomotion space needs its blend; parameter paths are cached names.
	if BLEND_PARAMETERS.has(next) and (next!=last_blend_state or blend.distance_squared_to(last_blend)>0.0004):
		tree.set(BLEND_PARAMETERS[next],blend)
		last_blend_state=next
		last_blend=blend
	var rate := 1.0
	if next in ["walk","jog","sprint","crouch","crawl"]:
		# Pro has forward-only walk/sprint: other directions use retimed authored jogs.
		var base := 1.2 if next=="crawl" else 2.0 if next=="crouch" else 2.2 if next=="walk" else 7.0 if next=="sprint" else 4.5
		rate=clampf(speed/base,0.65,1.8) if speed>0.2 else 1.0
	if absf(rate-last_rate)>0.01:
		tree.set(RATE_PARAMETER,rate)
		last_rate=rate
	tree.advance(delta)
	grounded_before=motion_grounded
	pulse_before=motion_pulse
	stance_before=stance
	dead_before=dead
