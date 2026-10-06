extends "res://network_node.gd"
## The public C# NetBox3D adapter owns stepping, rather than the base native fixture.
var box_world: RefCounted
var adapter: RefCounted

func baseline_ok() -> bool:
	box_world = managed.GetBoxState().world
	var row: Dictionary = client_bridge.get_entity(entity)
	if row.is_empty() or row.authority_peer != peer:
		return false
	var state: Dictionary = row.state
	return state.get("sequence") == baseline_sequence and state.get("blob") == PackedByteArray([0, 255, 42]) and state.get("physics_tick", 0) > 0 and state.physics_tick <= box_world.get_tick()

func snapshot(action: String) -> Dictionary:
	var proof := super.snapshot(action)
	var state: Dictionary = managed.GetBoxState()
	adapter = state.adapter
	box_world = state.world
	var body: Dictionary = box_world.get_body_state(BODY_ID)
	var cpp: Dictionary = native.get_physics_state()
	var cs: Dictionary = managed.GetPhysicsState()
	var row: Dictionary = client_bridge.get_entity(entity)
	var received: Dictionary = row.get("state", {})
	var mapped: Dictionary = adapter.get("_tracked")
	var attached: bool = adapter.get("_net") == server_bridge and adapter.get("_world") == box_world
	var connections := {}
	for signal_name in ["before_step", "after_step", "failed"]:
		connections[signal_name] = adapter.get_signal_connection_list(signal_name).size()
	var clock_connections := server_bridge.get_signal_connection_list("simulation_tick").size()
	var adapter_connections := 0
	for connection in server_bridge.get_signal_connection_list("simulation_tick"):
		if connection.callable.get_object() == adapter:
			adapter_connections += 1
	# Keep high-level forwarding evidence separate from the adapter's own clock link.
	proof.node_state.server_connections.simulation_tick -= adapter_connections
	proof.box3d = {"adapter_id": str(adapter.get_instance_id()), "world_id": str(box_world.get_instance_id()),
		"tick": box_world.get_tick(), "hash": box_world.get_state_hash(), "body_id": BODY_ID,
		"body_count": box_world.get_body_count(), "position_y": body.position.y, "velocity_y": body.linear_velocity.y,
		"before": state.before, "after": state.after, "failures": state.failures,
		"handoffs": state.handoffs, "restores": state.restores, "checks": state.checks,
		"capsule_empty": state.capsule_empty, "connections": connections, "clock_connections": clock_connections,
		"adapter_connections": adapter_connections, "attached": attached, "mapping": mapped.duplicate(),
		"entity": state.entity, "clock_offset": state.clock_offset, "client_tick": received.get("physics_tick", 0),
		"client_position_y": received.position.y if received.has("position") else 0.0,
		"references_ok": native.network_state.get("world") == box_world and managed.NetworkState.get("world") == box_world,
		"cpp_state_ok": cpp.get("position") == body.position and cpp.get("tick") == box_world.get_tick() and cpp.get("hash") == box_world.get_state_hash(),
		"cs_state_ok": cs.get("position") == body.position and cs.get("tick") == box_world.get_tick() and cs.get("hash") == box_world.get_state_hash()}
	if not received.is_empty():
		proof.baseline_state = {"sequence": received.sequence, "blob_hex": received.blob.hex_encode()}
	check(attached and state.before == box_world.get_tick() and state.after == state.before and state.failures == 0, "Adapter detached, duplicated steps or failed")
	proof.passed = error_message.is_empty()
	proof.error = error_message
	return proof

func run_action(action: String) -> Dictionary:
	# C# constructs the world before configuration and attaches before hosting.
	# Preserve it in both language dictionaries, including fresh-session reentry.
	return await super.run_action(action)

func close() -> void:
	running = false
	managed.CloseBox()
	super.close()
