extends "res://main.gd"
## Trusted local solver checkpoints stay in the authoritative process.
const PhysicsBridge = preload("res://addons/egp_net/egp_net_box3d.gd")
var world: RefCounted
var physics: RefCounted
var checkpoint_bytes := PackedByteArray()
var physics_history: Dictionary = {}
var physics_mappings: Dictionary = {}

func configure() -> bool:
	if world == null:
		world = ClassDB.instantiate("EGPBox3DWorld")
		if world == null or world.configure(60, 4, 1, Vector3(0, -9.8, 0)) != OK:
			finish(false, "physics world configuration")
			return false
		if role != "client" and (world.queue_create_box(1, 1, Vector3(0, -1, 0), Vector3(200, 1, 20), 0) != OK or world.apply_queued_commands() != OK):
			finish(false, "physics floor creation")
			return false
	simulation_fingerprint = world.get_simulation_fingerprint()
	if not super.configure():
		return false
	if role != "client" and physics == null:
		physics = PhysicsBridge.new()
		physics.failed.connect(func(error: Error): finish(false, "physics adapter failed: %d" % error))
		if physics.attach(net, world) != OK:
			finish(false, "physics adapter attachment")
			return false
	return true

func receive_hello(peer: int, arguments: Array) -> void:
	super.receive_hello(peer, arguments)
	if finished:
		return
	var client: int = arguments[0]
	var body := 10000 + client
	if world.get_body_state(body).is_empty():
		if world.queue_create_sphere(body, 1, Vector3(client * 3, 10, 0), 0.5) != OK or world.apply_queued_commands() != OK:
			finish(false, "stable physics body creation")
			return
	var owned: int = owned_entities[client]
	# Network connection handles and snapshot body IDs belong to different domains.
	if physics.track(owned, body) != OK:
		finish(false, "stable physics body mapping")
		return
	var mapping: Dictionary = physics_mappings.get(epoch, {})
	mapping[client] = {"entity": owned, "body": body}
	physics_mappings[epoch] = mapping

func receive_input(peer: int, handle: int, input: Dictionary) -> void:
	super.receive_input(peer, handle, input)
	if not finished and world.queue_impulse(input.account, input.generation, Vector3(input.account - 9999, 0, 0)) != OK:
		finish(false, "owner physics impulse")

func valid_owner_state(state: Dictionary) -> bool:
	var metadata := state.duplicate(true)
	for field in ["position", "rotation", "linear_velocity", "angular_velocity", "transform", "physics_tick"]:
		metadata.erase(field)
	return super.valid_owner_state(metadata)

func capture_checkpoint() -> Dictionary:
	var checkpoint := super.capture_checkpoint()
	checkpoint_bytes = world.capture_snapshot()
	if checkpoint_bytes.is_empty():
		finish(false, "physics checkpoint has pending commands or failed")
		return checkpoint
	var ids: Array[int] = [1]
	for client in range(clients):
		ids.append(10000 + client)
	checkpoint.physics = {"tick": world.get_tick(), "hash": world.get_state_hash(), "bodies": world.get_body_count(), "body_ids": ids, "bytes": checkpoint_bytes.size()}
	return checkpoint

func advance_checkpoint_branch() -> bool:
	if world.queue_impulse(10000, 1, Vector3(100, 0, 0)) != OK:
		return false
	for step in range(6):
		if world.step_tick(world.get_tick() + 1) != OK:
			return false
	return true

func recover_server_stall(error: Error) -> bool:
	if error != FAILED or checkpoint_bytes.is_empty():
		return false
	physics.detach()
	var saved: Dictionary = server_stall_proof.checkpoint.physics
	if not advance_checkpoint_branch():
		return false
	var branch_hash: String = world.get_state_hash()
	var branch_tick: int = world.get_tick()
	var damaged := checkpoint_bytes.duplicate()
	damaged[damaged.size() - 1] ^= 1
	var rejected: Error = world.restore_snapshot(damaged)
	if rejected == OK or world.get_state_hash() != branch_hash or world.get_tick() != branch_tick:
		return false
	if world.restore_snapshot(checkpoint_bytes) != OK or world.get_state_hash() != saved.hash or world.get_tick() != saved.tick or world.get_body_count() != saved.bodies:
		return false
	for body in saved.body_ids:
		if world.get_body_state(body).is_empty():
			return false
	# Replay the identical local command branch from the restored full solver state.
	if not advance_checkpoint_branch() or world.get_state_hash() != branch_hash or world.restore_snapshot(checkpoint_bytes) != OK or world.get_state_hash() != saved.hash:
		return false
	server_stall_proof.physics = {"restored_tick": world.get_tick(), "restored_hash": world.get_state_hash(), "restored_bodies": world.get_body_count(), "branch_tick": branch_tick, "branch_hash": branch_hash, "replayed_hash": branch_hash, "damaged_restore_error": rejected, "damaged_restore_preserved_hash": branch_hash}
	if not super.recover_server_stall(error):
		return false
	return physics.attach(net, world) == OK

func _process(delta: float) -> void:
	super._process(delta)
	if finished or role != "client" or net == null or owned_entity == 0:
		return
	var record: Dictionary = net.get_entity(owned_entity)
	if record.is_empty() or not record.state.has("physics_tick"):
		return
	var state: Dictionary = record.state
	if not state.get("position") is Vector3 or not state.get("linear_velocity") is Vector3 or state.get("physics_tick", 0) < 1:
		finish(false, "replicated physics state contract")
		return
	physics_history[epoch] = {"tick": state.physics_tick, "entity": owned_entity, "position": [state.position.x, state.position.y, state.position.z], "linear_velocity": [state.linear_velocity.x, state.linear_velocity.y, state.linear_velocity.z]}

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	if passed:
		if role == "client":
			passed = physics_history.has(1) and physics_history.has(2) and physics_history[2].tick > physics_history[1].tick and physics_history[2].entity != physics_history[1].entity
		else:
			passed = server_stall_proof.has("physics") and physics_mappings.size() == 2 and world.get_tick() > server_stall_proof.physics.restored_tick
	server_stall_proof.physics_history = physics_history
	server_stall_proof.physics_mappings = physics_mappings
	if world != null:
		server_stall_proof.physics_final_tick = world.get_tick()
		server_stall_proof.physics_final_hash = world.get_state_hash()
	if physics != null:
		physics.detach()
	super.finish(passed, message if passed else "physics checkpoint/mapped replication failed: " + message)
