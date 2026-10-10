extends RefCounted

# Physics exhibits built entirely from native deterministic Box3D bodies and joints in
# the shared EGPBox3DWorld: buoyant floats, dominoes, a jointed cloth, a spherical-joint
# chain and a sprung prismatic slider. Every peer that applies the same command stream
# simulates them bit-identically; nothing here uses scene physics.
const WATER := Vector3(-13,1.9,-11)
const CLOTH := Vector3(13,0,-11)
const JOINTS := Vector3(-13,0,11)
const IMPACT := Vector3(13,0,11)
const FLOAT_BASE := 2100
const CLOTH_BASE := 2500
const CHAIN_BASE := 2600
const SPRING_ANCHOR := 2606
const SPRING_BODY := 2607
const CLOTH_SIZE := 8
const CLOTH_SPACING := 0.5
const PARTICLE_RADIUS := 0.07
const CLOTH_JOINT_BASE := 1
const CHAIN_JOINT_BASE := 500
const SPRING_JOINT := 600
const FLOAT_DENSITIES := [0.35,0.85,1.8]
# Exhibit blocks knocked off the arena fall forever; below this plane they return home.
const KILL_PLANE := -20.0
const RESPAWN_SEQUENCE := 900000

var world: EGPBox3DWorld
var original := PackedVector3Array()
var interactions := 0
var samples := 0
var max_deformation := 0.0
var joint_motion := 0.0
var previous := PackedVector3Array()
var active := false
var cloth_ids := PackedInt64Array()


static func indices() -> PackedInt32Array:
	var result := PackedInt32Array()
	for y in range(CLOTH_SIZE-1):
		for x in range(CLOTH_SIZE-1):
			var a := y*CLOTH_SIZE+x
			result.append_array(PackedInt32Array([a,a+1,a+CLOTH_SIZE,a+1,a+CLOTH_SIZE+1,a+CLOTH_SIZE]))
	return result


static func in_water(p: Vector3) -> bool:
	return absf(p.x-WATER.x)<5.8 and absf(p.z-WATER.z)<5.8 and p.y<3.0


static func pinned(index: int) -> bool:
	return index == 0 or index == CLOTH_SIZE-1


static func joint_bodies() -> Array[int]:
	var result: Array[int] = []
	for i in range(5):
		result.append(CHAIN_BASE+1+i)
	result.append(SPRING_BODY)
	return result


func _cloth_rest(index: int) -> Vector3:
	return CLOTH+Vector3((index % CLOTH_SIZE-3.5)*CLOTH_SPACING,4.8-(index/CLOTH_SIZE)*CLOTH_SPACING,0)


func setup(target: EGPBox3DWorld, _total: int, create_bodies: bool = true) -> bool:
	world = target
	original.clear()
	for i in range(CLOTH_SIZE*CLOTH_SIZE):
		original.append(_cloth_rest(i))
	active = true
	# Native Box3D fluid volume (Archimedes lift and drag inside step_tick). Fluid
	# configuration is not part of snapshots, so every peer applies it here.
	world.configure_fluid(Vector3(WATER.x-5.8,-4,WATER.z-5.8),Vector3(WATER.x+5.8,WATER.y,WATER.z+5.8),1.0,2.0,1.0)
	for i in range(6):
		world.set_body_buoyant(FLOAT_BASE+i)
	if not create_bodies:
		# A joining client restored every exhibit body and joint from the snapshot.
		return true
	var ok := true
	# Equal-volume objects with different density demonstrate float versus sink.
	for i in range(6):
		ok = ok and world.queue_create_box(FLOAT_BASE+i,0,_exhibit_home(i),Vector3.ONE*0.5,2,FLOAT_DENSITIES[i%3]) == OK
	# Native rigid dominoes, independently identified in the same world.
	for i in range(16):
		ok = ok and world.queue_create_box(FLOAT_BASE+6+i,0,_exhibit_home(6+i),Vector3(0.12,0.8,0.5),2,0.6) == OK
	# Cloth: particle spheres; the two top corners are static pins.
	for i in range(CLOTH_SIZE*CLOTH_SIZE):
		ok = ok and world.queue_create_sphere(CLOTH_BASE+i,0,original[i],PARTICLE_RADIUS,0 if pinned(i) else 2,20.0) == OK
	# Chain: a static hook and five links on spherical joints.
	ok = ok and world.queue_create_box(CHAIN_BASE,0,JOINTS+Vector3(-2,5.5,0),Vector3.ONE*0.15,0,1.0) == OK
	for i in range(5):
		ok = ok and world.queue_create_box(CHAIN_BASE+1+i,0,JOINTS+Vector3(-2,4.8-i*0.8,0),Vector3(0.18,0.35,0.18),2,1.0) == OK
	# Slider: a static rail anchor and a block on a sprung, limited prismatic joint.
	ok = ok and world.queue_create_box(SPRING_ANCHOR,0,JOINTS+Vector3(2,2,0),Vector3.ONE*0.2,0,1.0) == OK
	ok = ok and world.queue_create_box(SPRING_BODY,0,JOINTS+Vector3(3,2,0),Vector3.ONE*0.45,2,0.5) == OK
	var joint := CLOTH_JOINT_BASE
	# Structural (horizontal/vertical), shear (diagonal) and bending (skip-one) links.
	for y in range(CLOTH_SIZE):
		for x in range(CLOTH_SIZE):
			var a := y*CLOTH_SIZE+x
			var links := []
			if x+1 < CLOTH_SIZE: links.append([a+1,CLOTH_SPACING,false])
			if y+1 < CLOTH_SIZE: links.append([a+CLOTH_SIZE,CLOTH_SPACING,false])
			if x+1 < CLOTH_SIZE and y+1 < CLOTH_SIZE: links.append([a+CLOTH_SIZE+1,CLOTH_SPACING*sqrt(2.0),true])
			if x > 0 and y+1 < CLOTH_SIZE: links.append([a+CLOTH_SIZE-1,CLOTH_SPACING*sqrt(2.0),true])
			if x+2 < CLOTH_SIZE: links.append([a+2,CLOTH_SPACING*2,true])
			if y+2 < CLOTH_SIZE: links.append([a+2*CLOTH_SIZE,CLOTH_SPACING*2,true])
			for link in links:
				var b: int = link[0]
				if pinned(a) and pinned(b):
					continue
				var options := {"length":link[1]}
				if link[2]:
					options.merge({"enable_spring":true,"hertz":6.0,"damping_ratio":0.3})
				ok = ok and world.queue_create_joint(joint,0,EGPBox3DWorld.JOINT_DISTANCE,CLOTH_BASE+a,CLOTH_BASE+b,Vector3.ZERO,Vector3.ZERO,options) == OK
				joint += 1
	var parent := CHAIN_BASE
	for i in range(5):
		var anchor := Vector3(0,-0.3,0) if i == 0 else Vector3(0,-0.4,0)
		ok = ok and world.queue_create_joint(CHAIN_JOINT_BASE+i,0,EGPBox3DWorld.JOINT_SPHERICAL,parent,CHAIN_BASE+1+i,anchor,Vector3(0,0.4,0)) == OK
		parent = CHAIN_BASE+1+i
	ok = ok and world.queue_create_joint(SPRING_JOINT,0,EGPBox3DWorld.JOINT_PRISMATIC,SPRING_ANCHOR,SPRING_BODY,Vector3(1,0,0),Vector3.ZERO,
		{"axis":Vector3(1,0,0),"enable_spring":true,"hertz":0.8,"damping_ratio":0.05,"enable_limit":true,"lower":-2.0,"upper":2.0}) == OK
	ok = ok and world.apply_queued_commands() == OK
	# Initial motion so the chain swings and the slider oscillates.
	ok = ok and world.queue_impulse(CHAIN_BASE+5,1,Vector3(2,0,2)*0.25) == OK
	ok = ok and world.queue_impulse(SPRING_BODY,1,Vector3(3,0,0)*0.2) == OK
	ok = ok and world.apply_queued_commands() == OK
	active = ok
	return ok


# Rest pose of exhibit block i: six floats, then sixteen dominoes.
func _exhibit_home(i: int) -> Vector3:
	if i < 6:
		return WATER+Vector3((i%3-1)*2,0.1,(i/3-0.5)*3)
	var d := i-6
	return IMPACT+Vector3((d%8-3.5)*0.85,0.8,float(d/8)*2)


func step(target: EGPBox3DWorld, actors: Array[Dictionary], time: float) -> void:
	world = target
	var tick := world.get_tick()+1
	if tick % 30 == 0:
		# Deterministic kill plane: every peer reads the same state and queues the same reset.
		for i in range(22):
			var state: Dictionary = world.get_body_state(FLOAT_BASE+i)
			if not state.is_empty() and state.position.y < KILL_PLANE:
				world.queue_body_state(FLOAT_BASE+i,RESPAWN_SEQUENCE,_exhibit_home(i),Quaternion.IDENTITY,Vector3.ZERO,Vector3.ZERO)
	if tick % 3 == 0:
		# Deterministic gusting wind on every free cloth particle, one batched call.
		var ids := PackedInt64Array()
		var impulses := PackedFloat32Array()
		for i in range(CLOTH_SIZE*CLOTH_SIZE):
			if not pinned(i):
				ids.append(CLOTH_BASE+i)
				impulses.append_array(PackedFloat32Array([0.0006*sin(time+i*0.2),0,0.0028+0.002*sin(time*1.7)]))
		world.queue_impulses(ids,300,impulses)
	samples += 1


# Presentation and qualification statistics (per process, never fed back into physics).
func observe() -> void:
	var points := cloth_points()
	for i in range(points.size()):
		max_deformation = maxf(max_deformation,points[i].distance_to(original[i]))
	var poses := joint_poses()
	var positions: PackedVector3Array = poses[0]
	if previous.size() == positions.size():
		for i in range(positions.size()):
			joint_motion += positions[i].distance_to(previous[i])
	previous = positions


func interact(p: Vector3) -> void:
	interactions += 1
	if p.distance_to(CLOTH) < 9:
		for i in range(CLOTH_SIZE, CLOTH_SIZE*CLOTH_SIZE):
			world.queue_impulse(CLOTH_BASE+i,301,Vector3(0,0,0.02))
	elif p.distance_to(JOINTS) < 9:
		world.queue_impulse(CHAIN_BASE+5,301,Vector3(0.5,0,0.25))
		world.queue_impulse(SPRING_BODY,301,Vector3(0.6,0,0))


func cloth_points() -> PackedVector3Array:
	if cloth_ids.is_empty():
		for i in range(CLOTH_SIZE*CLOTH_SIZE):
			cloth_ids.append(CLOTH_BASE+i)
	var states: PackedFloat32Array = world.get_body_states(cloth_ids)
	var points := PackedVector3Array()
	points.resize(CLOTH_SIZE*CLOTH_SIZE)
	for i in range(CLOTH_SIZE*CLOTH_SIZE):
		points[i] = Vector3(states[i*13],states[i*13+1],states[i*13+2])
	return points


func joint_poses() -> Array:
	var positions := PackedVector3Array()
	var rotations: Array[Quaternion] = []
	for id in joint_bodies():
		var state := world.get_body_state(id)
		positions.append(state.position)
		rotations.append(state.rotation)
	return [positions,rotations]


func compact_packet(sequence: int, tick: int, time: float) -> PackedByteArray:
	# X103: cloth and joint poses in one 488-byte frame for clients that do not
	# simulate locally, millimetre/short-quaternion quantized, stamped with sim time.
	var bytes := "X103".to_ascii_buffer()
	bytes.resize(20)
	bytes.encode_u32(4,sequence)
	bytes.encode_u32(8,tick)
	bytes.encode_float(12,time)
	bytes.encode_u16(16,CLOTH_SIZE*CLOTH_SIZE)
	bytes.encode_u16(18,6)
	var record := PackedByteArray()
	record.resize(6)
	for p in cloth_points():
		var local: Vector3 = (p-CLOTH)*1000.0
		for axis in range(3):
			record.encode_s16(axis*2,clampi(roundi(local[axis]),-32767,32767))
		bytes.append_array(record)
	var joint := PackedByteArray()
	joint.resize(14)
	var poses := joint_poses()
	for i in range(6):
		var local: Vector3 = (poses[0][i]-JOINTS)*1000.0
		var q: Quaternion = poses[1][i]
		for axis in range(3):
			joint.encode_s16(axis*2,clampi(roundi(local[axis]),-32767,32767))
		var values := [q.x,q.y,q.z,q.w]
		for i2 in range(4):
			joint.encode_s16(6+i2*2,roundi(clampf(values[i2],-1,1)*32767))
		bytes.append_array(joint)
	return bytes


func telemetry(target: EGPBox3DWorld) -> Dictionary:
	world = target
	observe()
	var points := cloth_points()
	var finite := true
	for p in points:
		finite = finite and p.is_finite() and p.length() < 100
	var pin_error := maxf(points[0].distance_to(original[0]),points[CLOTH_SIZE-1].distance_to(original[CLOTH_SIZE-1]))
	var heights := []
	for i in range(6):
		heights.append(world.get_body_state(FLOAT_BASE+i).position.y)
	return {"backend":"EGPBox3DWorld","cloth_vertices":CLOTH_SIZE*CLOTH_SIZE,"cloth_finite":finite,"pin_error":pin_error,"cloth_deformation":max_deformation,
		"joint_travel":joint_motion,"joints":world.get_joint_count(),"float_heights":heights,"interactions":interactions,"samples":samples}


func close() -> void:
	active = false
