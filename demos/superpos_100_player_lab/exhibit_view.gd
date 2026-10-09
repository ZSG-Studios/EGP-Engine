extends Node3D

const SIM := preload("res://simulation_world.gd")
var cloth_mesh: MeshInstance3D
var cloth_points := PackedVector3Array()
var cloth_target := PackedVector3Array()
var joint_targets := PackedVector3Array()
var joint_rotations: Array[Quaternion] = []
var joints: Array[MeshInstance3D] = []
var chain_lines: Array[MeshInstance3D] = []
var time := 0.0

func _ready() -> void:
	for station in [SIM.WATER,SIM.CLOTH,SIM.JOINTS,SIM.IMPACT]:
		_box(Vector3(station.x,0.035,station.z),Vector3(12,0.07,12),Color("243d56"))
	for x in [-6.1,6.1]:
		_box(SIM.WATER+Vector3(x,-0.9,0),Vector3(0.2,2,12.4),Color("486c83"))
		_box(SIM.WATER+Vector3(0,-0.9,x),Vector3(12.4,2,0.2),Color("486c83"))
	var water := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size=Vector2(11.6,11.6)
	plane.subdivide_width=32
	plane.subdivide_depth=32
	water.mesh=plane
	var shader := Shader.new()
	shader.code="""shader_type spatial;
render_mode cull_disabled, depth_draw_opaque;
void vertex() { VERTEX.y += sin(VERTEX.x*1.6+TIME*1.1)*0.04 + cos(VERTEX.z*1.9+TIME*1.4)*0.03; }
void fragment() { ALBEDO=vec3(0.05,0.42,0.56); METALLIC=0.25; ROUGHNESS=0.18; ALPHA=0.57; }
"""
	var material := ShaderMaterial.new()
	material.shader=shader
	water.material_override=material
	water.position=SIM.WATER
	add_child(water)
	_label(SIM.WATER+Vector3(0,4,-5.8),"01 / BUOYANCY POOL\nGREEN FLOATS / GOLD NEUTRAL / PINK SINKS\nENTER TO SWIM",Color("62efd1"))
	_label(SIM.CLOTH+Vector3(0,6,0),"02 / NATIVE CLOTH\nPINNED FABRIC + WIND + BODY CONTACTS\nE: WIND IMPULSE",Color("c9a4ff"))
	_label(SIM.JOINTS+Vector3(0,6,0),"03 / JOINT LAB\nPIN CHAIN + LIMITED 6DOF SPRING\nE: EXCITE",Color("ffcc7d"))
	_label(SIM.IMPACT+Vector3(0,4,0),"04 / IMPACT COURSE\nDOMINO COLLISIONS + DYNAMIC BOXES\nE: TRIGGER / F: SHOCKWAVE",Color("83baff"))
	for x in [-1.9,1.9]:
		_box(SIM.CLOTH+Vector3(x,2.5,0),Vector3(0.12,5,0.12),Color("8299ad"))
	_box(SIM.CLOTH+Vector3(0,4.95,0),Vector3(4,0.12,0.12),Color("8299ad"))
	cloth_mesh=MeshInstance3D.new()
	cloth_mesh.mesh=ArrayMesh.new()
	var fabric := StandardMaterial3D.new()
	fabric.albedo_color=Color("b691eb")
	fabric.cull_mode=BaseMaterial3D.CULL_DISABLED
	fabric.roughness=0.9
	cloth_mesh.material_override=fabric
	add_child(cloth_mesh)
	_box(SIM.JOINTS+Vector3(-2,5.6,0),Vector3(1.5,0.2,1.5),Color("6a879e"))
	_box(SIM.JOINTS+Vector3(2,2,0),Vector3(0.4,0.4,0.4),Color("75879f"))
	for i in range(6):
		joints.append(_box(Vector3.ZERO,Vector3(0.36,0.7,0.36) if i<5 else Vector3.ONE*0.9,Color("ffc170")))
		joints[i].visible=false
		if i<5:
			chain_lines.append(_box(Vector3.ZERO,Vector3(0.07,1,0.07),Color("f6e5b9")))
	# Low benches provide the chair-sitting animation showcase.
	_box(Vector3(0,0.48,-27),Vector3(3,0.16,0.65),Color("6589a2"))
	_box(Vector3(0,1,-27.38),Vector3(3,0.9,0.12),Color("6589a2"))
	for x in [-1.2,1.2]:
		_box(Vector3(x,0.24,-27),Vector3(0.12,0.48,0.5),Color("455970"))
	_label(Vector3(0,3,-27),"PLAYER DECK / T: SIT / G: GROUND SIT\nC: CRAWL / CTRL: CROUCH / K: DEATH / R: RESPAWN",Color("e8f7ff"))

func _box(p: Vector3,size: Vector3,color: Color) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size=size
	node.mesh=mesh
	var material := StandardMaterial3D.new()
	material.albedo_color=color
	material.roughness=0.55
	node.material_override=material
	add_child(node)
	node.position=p
	return node

func _label(p: Vector3,text: String,color: Color) -> void:
	var label := Label3D.new()
	label.text=text
	label.position=p
	label.font_size=38
	label.pixel_size=0.006
	label.modulate=color
	label.billboard=BaseMaterial3D.BILLBOARD_ENABLED
	add_child(label)

# Timeline of compact frames [time, cloth points, joint positions, joint rotations].
var frames: Array = []
var target_time := 0.0

func push_frame(time: float,points: PackedVector3Array,positions: PackedVector3Array,rotations: Array[Quaternion]) -> void:
	frames.append([time,points,positions,rotations])
	if frames.size()>16:
		frames.pop_front()

func _timeline() -> void:
	# Interpolate cloth vertices and joint poses at the lab's interpolation time.
	if frames.is_empty():
		return
	var before: Array=frames[0]
	var after: Array=frames[0]
	for frame in frames:
		if frame[0]<=target_time:
			before=frame
			after=frame
		else:
			after=frame
			break
	var weight := 0.0 if after[0]<=before[0] else clampf((target_time-before[0])/(after[0]-before[0]),0,1)
	var points := PackedVector3Array()
	points.resize(64)
	for i in range(64):
		points[i]=before[1][i].lerp(after[1][i],weight)
	cloth_target=points
	if cloth_points.is_empty():
		cloth_points=points.duplicate()
	var positions := PackedVector3Array()
	var rotations: Array[Quaternion] = []
	for i in range(before[2].size()):
		positions.append(before[2][i].lerp(after[2][i],weight))
		rotations.append(before[3][i].slerp(after[3][i],weight))
	joint_targets=positions
	joint_rotations=rotations

func set_live(points: PackedVector3Array,positions: PackedVector3Array,rotations: Array[Quaternion]) -> void:
	# Deterministic clients simulate cloth and joints locally every tick: show them as-is.
	frames.clear()
	if points.size()==64:
		cloth_target=points
		cloth_points=points.duplicate()
	if positions.size()==6:
		joint_targets=positions
		joint_rotations=rotations
		for i in range(6):
			joints[i].position=positions[i]
			joints[i].quaternion=rotations[i]

func apply_frame(kind: int,points: PackedVector3Array,rotations: Array[Quaternion]) -> void:
	if kind==0:
		cloth_target=points
		if cloth_points.is_empty():
			cloth_points=points.duplicate()
	else:
		joint_targets=points
		joint_rotations=rotations

func _process(delta: float) -> void:
	_timeline()
	if cloth_target.size()==64:
		for i in range(64):
			cloth_points[i]=cloth_points[i].lerp(cloth_target[i],1-exp(-delta*30))
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX]=cloth_points
		arrays[Mesh.ARRAY_INDEX]=SIM.indices()
		var mesh: ArrayMesh=cloth_mesh.mesh
		if mesh.get_surface_count()==0:
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays,[],{},Mesh.ARRAY_FLAG_USE_DYNAMIC_UPDATE)
			cloth_mesh.custom_aabb=AABB(SIM.CLOTH+Vector3(-7,-1,-7),Vector3(14,9,14))
		else:
			mesh.surface_update_vertex_region(0,0,cloth_points.to_byte_array())
	if joint_targets.size()==6:
		for i in range(6):
			if not joints[i].visible:
				joints[i].position=joint_targets[i]
			joints[i].visible=true
			joints[i].position=joints[i].position.lerp(joint_targets[i],1-exp(-delta*30))
			joints[i].quaternion=joints[i].quaternion.slerp(joint_rotations[i],1-exp(-delta*30))
			if i<5:
				var from := SIM.JOINTS+Vector3(-2,5.5,0) if i==0 else joints[i-1].position
				var to := joints[i].position
				chain_lines[i].position=(from+to)/2
				chain_lines[i].scale.y=from.distance_to(to)
				chain_lines[i].quaternion=Quaternion(Vector3.UP,(from-to).normalized())
