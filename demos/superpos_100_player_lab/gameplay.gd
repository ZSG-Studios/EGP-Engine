extends RefCounted

# Deterministic gameplay: the single tick-based step the dedicated server and every
# deterministic client run on an EGPBox3DWorld. It reads only world state and the
# per-tick command inputs, never wall-clock time, so peers that start from the same
# snapshot and apply the same Superpos command stream compute bit-identical physics.
const SIMULATION := preload("res://simulation_world.gd")
const PLAYGROUND := preload("res://playground.gd")
const PLAYER_BASE := 1000
const PROP_BASE := 2000
const TICK_RATE := 60
const INPUT_BYTES := 8
# Persistent control bits (sprint and stances) versus one-shot actions.
const PERSISTENT := 4|8|16|32|64|128|256
const ONE_SHOT := 1|2|1024|2048
const HUMAN := 100
const GOALS := [Vector3(-24,0,-24),Vector3(-13,0,-11),Vector3(24,0,-24),Vector3(13,0,-11),Vector3(24,0,24),Vector3(13,0,11),Vector3(-24,0,24),Vector3(-13,0,11)]
const STANCE_ORDER := [256,128,64,16,8,32]
const CHAIR := Vector3(0,0,-27)

var world: EGPBox3DWorld
var simulation: RefCounted
var total := 0
var actors: Array[Dictionary] = []
var movable: Array[int] = []
var dynamic_count := 0
var player_ids := PackedInt64Array()
var movable_ids := PackedInt64Array()


static func spawn(id: int) -> Vector3:
	if id == HUMAN:
		return Vector3(0, 1.2, -26)
	return Vector3((id % 10 - 4.5) * 5.1, 1.2, (floori(float(id) / 10) - 4.5) * 5.1)


static func goal(index: int) -> Vector3:
	return GOALS[index % 8]


static func speed(stance: int, sprinting: bool) -> float:
	if stance&(64|128|256):
		return 0
	if stance&16:
		return 1.2
	if stance&8:
		return 2.0
	if stance&32:
		return 1.1
	return 8.5 if sprinting else 4.5


static func mass(half_height: float) -> float:
	return 0.85*(PI*0.35*0.35*(half_height*2)+4.0/3.0*PI*pow(0.35,3))


# Input wire: dx s8, dz s8, flags u16, facing u16, client tick low 16 bits.
static func encode_input(direction: Vector3, flags: int, facing: float, tick: int) -> PackedByteArray:
	var bytes := PackedByteArray()
	bytes.resize(INPUT_BYTES)
	bytes.encode_s8(0, clampi(roundi(direction.x*127.0),-127,127))
	bytes.encode_s8(1, clampi(roundi(direction.z*127.0),-127,127))
	bytes.encode_u16(2, flags & 4095)
	bytes.encode_u16(4, roundi((wrapf(facing,-PI,PI)+PI)/TAU*65535.0))
	bytes.encode_u16(6, tick & 65535)
	return bytes


static func decode_direction(bytes: PackedByteArray) -> Vector3:
	if bytes.size() != INPUT_BYTES:
		return Vector3.ZERO
	return Vector3(float(bytes.decode_s8(0))/127.0, 0, float(bytes.decode_s8(1))/127.0).limit_length(1.0)


static func decode_flags(bytes: PackedByteArray) -> int:
	return bytes.decode_u16(2) if bytes.size() == INPUT_BYTES else 0


static func decode_facing(bytes: PackedByteArray) -> float:
	return float(bytes.decode_u16(4))/65535.0*TAU-PI if bytes.size() == INPUT_BYTES else 0.0


static func decode_tick16(bytes: PackedByteArray) -> int:
	return bytes.decode_u16(6) if bytes.size() == INPUT_BYTES else 0


func _new_actor(id: int) -> Dictionary:
	return {"id":id, "position":spawn(id), "stance":0, "collider_height":0.55, "cooldown":0, "pose_until":0, "interact_until":0,
		"facing":0.0, "sprinting":false, "direction":Vector3.ZERO, "score":0, "goal":id % 8, "tick16":0}


func _index_bodies() -> void:
	dynamic_count = PLAYGROUND.dynamic_bodies().size()
	movable.clear()
	for prop in range(40):
		movable.append(PROP_BASE+prop)
	for i in range(dynamic_count):
		movable.append(PLAYGROUND.BASE+i)
	movable_ids = PackedInt64Array(movable)
	player_ids.clear()
	for id in range(total):
		player_ids.append(PLAYER_BASE+id)


# Authoritative creation (server, or a client starting from tick zero).
func create(player_count: int) -> bool:
	total = player_count
	world = EGPBox3DWorld.new()
	if world.configure(TICK_RATE, 4, 1) != OK:
		return false
	var ok := world.queue_create_box(1,0,Vector3(0,-0.5,0),Vector3(35,0.5,35),0) == OK
	for id in range(total):
		ok = ok and world.queue_create_capsule(PLAYER_BASE+id,0,spawn(id),0.35,0.55,2,0.85) == OK
	for id in range(40):
		ok = ok and world.queue_create_box(PROP_BASE+id,0,Vector3((id % 8-3.5)*3.1,0.6,(floori(float(id)/8)-2)*3.1),Vector3.ONE*0.55,2,0.6) == OK
	ok = ok and PLAYGROUND.create(world)
	ok = ok and world.apply_queued_commands() == OK
	actors.clear()
	for id in range(total):
		actors.append(_new_actor(id))
	_index_bodies()
	simulation = SIMULATION.new()
	return ok and simulation.setup(world, total, true)


# Join keyframe: world snapshot plus gameplay actor state, zstd-compressed.
func capture() -> PackedByteArray:
	var snapshot := world.capture_snapshot()
	var state := var_to_bytes({"total":total, "actors":actors, "snapshot_size":snapshot.size()})
	var raw := PackedByteArray()
	raw.resize(8)
	raw.encode_u32(0, state.size())
	raw.encode_u32(4, snapshot.size())
	raw.append_array(state)
	raw.append_array(snapshot)
	var packed := PackedByteArray()
	packed.resize(4)
	packed.encode_u32(0, raw.size())
	packed.append_array(raw.compress(FileAccess.COMPRESSION_ZSTD))
	return packed


func restore(packed: PackedByteArray) -> bool:
	if packed.size() < 4:
		return false
	var raw := packed.slice(4).decompress(packed.decode_u32(0), FileAccess.COMPRESSION_ZSTD)
	if raw.size() < 8:
		return false
	var state_size := raw.decode_u32(0)
	var snapshot_size := raw.decode_u32(4)
	if raw.size() != 8+state_size+snapshot_size:
		return false
	var state = bytes_to_var(raw.slice(8, 8+state_size))
	if not state is Dictionary:
		return false
	world = EGPBox3DWorld.new()
	if world.configure(TICK_RATE, 4, 1) != OK or world.restore_snapshot(raw.slice(8+state_size)) != OK:
		return false
	total = int(state.total)
	actors.clear()
	for actor in state.actors:
		actors.append(actor)
	_index_bodies()
	simulation = SIMULATION.new()
	# World bodies came from the snapshot; only the local cloth/joint scene is created.
	return actors.size() == total and simulation.setup(world, total, false)


func grounded(position: Vector3, velocity: Vector3, props: Dictionary) -> bool:
	if absf(velocity.y) > 1.0:
		return false
	if position.y <= PLAYGROUND.support_height(position)+1.06:
		return true
	for state in props.values():
		var p: Vector3 = state
		if absf(position.y-(p.y+1.45)) < 0.12 and absf(position.x-p.x) < 0.85 and absf(position.z-p.z) < 0.85:
			return true
	return false


# One authoritative tick. inputs[slot] is the 8-byte command input of that player.
func step(tick: int, inputs: Array) -> void:
	var props := {}
	# One native read for every player; one batched write for every upright mover.
	var states: PackedFloat32Array = world.get_body_states(player_ids)
	var moved_ids := PackedInt64Array()
	var moved := PackedFloat32Array()
	for a in actors:
		var id: int = a.id
		var input: PackedByteArray = inputs[id]
		var flags := 0
		var direction := Vector3.ZERO
		if input.size() == INPUT_BYTES:
			flags = input.decode_u16(2)
			direction = Vector3(float(input.decode_s8(0))/127.0, 0, float(input.decode_s8(1))/127.0).limit_length(1.0)
			a.facing = float(input.decode_u16(4))/65535.0*TAU-PI
			a.tick16 = input.decode_u16(6)
		a.sprinting = (flags&4) != 0
		if flags&(1|2|32) and props.is_empty():
			# Prop states are read only when a jump, shockwave or push needs them.
			var prop_states: PackedFloat32Array = world.get_body_states(movable_ids)
			for i in range(movable.size()):
				props[movable[i]] = Vector3(prop_states[i*13],prop_states[i*13+1],prop_states[i*13+2])
		var o: int = id*13
		var velocity := Vector3(states[o+7],states[o+8],states[o+9])
		var p := Vector3(states[o],states[o+1],states[o+2])
		# Death persists until an explicit respawn. Chair sitting requires proximity.
		if not a.stance&256 or flags&1024:
			a.stance = 0
			for stance in STANCE_ORDER:
				if flags&stance:
					a.stance = stance
					break
			if a.stance&128 and p.distance_to(CHAIR) > 5:
				a.stance &= ~128
		if absf(p.x) > 31 or absf(p.z) > 31:
			direction = -Vector3(p.x,0,p.z).normalized()
		if flags&1024 and id == HUMAN:
			p = spawn(id)
			velocity = Vector3.ZERO
			a.stance = 0
		if a.stance&128:
			p = Vector3(0,0.9,-27)
			a.facing = 0.0
		var immobilized: bool = (a.stance&(64|128|256)) != 0
		if immobilized:
			direction = Vector3.ZERO
		var move_speed := speed(a.stance, a.sprinting)
		var half_height := 0.05 if a.stance&16 else 0.25 if a.stance&8 else 0.55
		var state_sequence := 0
		if half_height != a.collider_height:
			p.y += half_height-a.collider_height
			world.queue_destroy_body(PLAYER_BASE+id,0)
			world.queue_create_capsule(PLAYER_BASE+id,1,p,0.35,half_height,2,0.85)
			a.collider_height = half_height
			state_sequence = 2
		var wet := SIMULATION.in_water(p) and p.y < 2.5
		if wet and not immobilized:
			move_speed = 2.8
		a.direction = direction
		# Bookkeeping uses the pre-step position on every peer (one state read per actor).
		a.position = p
		var target: Vector3 = GOALS[int(a.goal) % 8]
		if Vector2(p.x-target.x,p.z-target.z).length() < 2:
			a.score += 1
			a.goal = (int(a.goal)+1) % 8
		# Upright authoritative capsules preserve solver position and vertical velocity.
		if state_sequence == 0:
			moved_ids.append(PLAYER_BASE+id)
			moved.append_array(PackedFloat32Array([p.x,p.y,p.z,0,0,0,1,direction.x*move_speed,velocity.y,direction.z*move_speed,0,0,0]))
		else:
			world.queue_body_state(PLAYER_BASE+id,state_sequence,p,Quaternion.IDENTITY,Vector3(direction.x*move_speed,velocity.y,direction.z*move_speed),Vector3.ZERO)
		if flags&1 and not immobilized and (grounded(p,velocity,props) or wet) and tick > a.cooldown:
			world.queue_impulse(PLAYER_BASE+id,3,Vector3.UP*(mass(a.collider_height)*5.5))
			a.cooldown = tick+45
		if wet:
			var fraction := clampf((SIMULATION.WATER.y-(p.y-a.collider_height-0.35))/(2*(a.collider_height+0.35)),0,1)
			var body_mass := mass(a.collider_height)
			# Hydrostatic displacement and drag applied to the native body.
			world.queue_impulse(PLAYER_BASE+id,4,Vector3.UP*(body_mass/0.85*9.81*fraction/60.0)-velocity*body_mass*fraction*1.8/60.0)
		if a.stance&32 and not immobilized:
			for prop in movable:
				if props[prop].distance_to(p) < 2.1:
					world.queue_impulse(prop,id+1,direction*0.045)
		if flags&2048 and tick > a.interact_until:
			simulation.interact(p)
			a.interact_until = tick+90
			if p.distance_to(SIMULATION.IMPACT) < 9:
				world.queue_impulse(SIMULATION.FLOAT_BASE+6,id+1,Vector3(0.4,0,0))
		if flags&2 and tick > a.cooldown:
			a.pose_until = tick+78
			for prop in movable:
				var away: Vector3 = props[prop]-p
				if away.length() < 7:
					world.queue_impulse(prop,id+102,away.normalized()*1.0+Vector3.UP*0.8)
			a.cooldown = tick+120
	world.queue_body_states(moved_ids,0,moved)
	simulation.step(world, actors, float(tick)/TICK_RATE)
	world.step_tick(tick)


# Replicated animation flags for presentation, derived from the deterministic state.
func motion_flags(id: int, tick: int, p: Vector3, velocity: Vector3) -> int:
	var a: Dictionary = actors[id]
	var flags := (1 if grounded(p,velocity,{}) else 0) | (2 if tick < int(a.pose_until) else 0) | int(a.stance) | (4 if a.sprinting else 0) | (512 if SIMULATION.in_water(p) and p.y < 2.5 else 0)
	return flags


func close() -> void:
	if simulation != null:
		simulation.close()
