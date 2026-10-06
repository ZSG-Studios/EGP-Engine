extends "res://network_base.gd"
## C# high-level helpers own the native endpoints and GDScript codec nodes.
var server_bridge: Node
var client_bridge: Node
const NODE_SIGNALS := ["state_changed", "peer_connected", "peer_disconnected", "entity_spawned", "entity_changed", "entity_despawned", "message_received", "input_received", "packet_received", "simulation_tick", "diagnostic"]

func baseline_ok() -> bool:
	var record: Dictionary = client_bridge.get_entity(entity)
	return not record.is_empty() and record.authority_peer == peer and record.state == {"sequence": baseline_sequence, "blob": PackedByteArray([0, 255, 42])}

func exchange() -> bool:
	check(managed.ExchangeNodeMessages(peer, entity, baseline_sequence) == OK, "typed messages/input/packet send failed")
	await wait_until(func():
		var state: Dictionary = managed.GetNodeState()
		return state.server_history.size() == baseline_sequence and state.client_history.size() == baseline_sequence and state.inputs == baseline_sequence and state.server_packets == baseline_sequence and state.client_packets == baseline_sequence)
	var tick: int = server.get_statistics().tick
	await wait_until(func(): return server.get_statistics().tick >= tick + 8)
	var state: Dictionary = managed.GetNodeState()
	check(state.server_messages == baseline_sequence and state.client_messages == baseline_sequence and state.inputs == baseline_sequence, "lost/duplicated high-level events or unowned input accepted")
	return error_message.is_empty()

func admit() -> bool:
	epoch += 1
	baseline_sequence = epoch
	check(managed.HostNode(port) == OK, "typed authority host failed")
	port = server.get_statistics().local_port
	var issued: Dictionary = managed.NodeToken("127.0.0.1:%d" % port)
	check(issued.error == OK and issued.token.size() == 2048, "typed admission issuance failed")
	check(managed.JoinNode(issued.token) == OK, "typed join failed")
	issued.token.clear()
	pause_authority = false
	running = true
	if not await wait_until(func(): return client.get_state() == "Connected"):
		return false
	var peers: Array = server_bridge.get_peers()
	check(peers.size() == 1 and peers[0].client_id == 424242, "typed admission account mismatch")
	peer = peers[0].peer_id
	entity = managed.SpawnNode(peer, baseline_sequence)
	check(entity != 0, "typed spawn failed")
	if not await wait_until(baseline_ok):
		return false
	return await exchange()

func snapshot(action: String) -> Dictionary:
	var proof := super.snapshot(action)
	proof.node_state = managed.GetNodeState()
	proof.node_state.server_connections = {}
	proof.node_state.client_connections = {}
	for signal_name in NODE_SIGNALS:
		proof.node_state.server_connections[signal_name] = server_bridge.get_signal_connection_list(signal_name).size()
		proof.node_state.client_connections[signal_name] = client_bridge.get_signal_connection_list(signal_name).size()
	proof.node_state.references_ok = str(server_bridge.get_instance_id()) == proof.node_state.server_bridge and str(client_bridge.get_instance_id()) == proof.node_state.client_bridge and proof.server_id == proof.node_state.server_native and proof.client_id == proof.node_state.client_native
	proof.baseline_state = client_bridge.get_entity(entity).get("state", {})
	if proof.baseline_state.has("blob"):
		proof.baseline_state.blob_hex = proof.baseline_state.blob.hex_encode()
		proof.baseline_state.erase("blob")
	return proof

func run_action(action: String) -> Dictionary:
	if action in ["network-start", "network-live-start"]:
		var config := {"game_protocol": "egp-node-reload-v1", "max_players": 1, "timeout_seconds": 3}
		if action == "network-live-start":
			simulation = JSON.parse_string(FileAccess.get_file_as_string("res://network_options.json"))
			config.merge(simulation)
		var created: Dictionary = managed.CreateNodeSessions(config)
		server = created.server
		client = created.client
		server_bridge = created.server_bridge
		client_bridge = created.client_bridge
		server.diagnostic.connect(func(message: String): diagnostics.append(message))
		client.state_changed.connect(func(state: String): states.append(state))
		server.application_received.connect(Callable(native, "receive_network"))
		server.application_received.connect(Callable(managed, "ReceiveNetwork"))
		var retained := {"server": server, "client": client}
		native.network_state = retained
		managed.NetworkState = retained
		await admit()
		return snapshot(action)
	if action == "network-live-check":
		check(references_ok() and server.get_state() == "Listening" and client.get_state() == "Connected", "high-level reload interrupted connection")
		baseline_sequence += 1
		check(managed.UpdateNode(entity, baseline_sequence) == OK, "typed entity update failed")
		await wait_until(baseline_ok)
		await exchange()
		return snapshot(action)
	if action in ["network-node-exit", "network-node-reenter"]:
		running = false
		if action == "network-node-exit":
			managed.RemoveNodeSessions()
		else:
			managed.ReenterNodeSessions()
		return snapshot(action)
	# Reuse the exact native fault budget and explicit fresh-admission/retired-handle checks.
	return await super.run_action(action)

func close() -> void:
	running = false
	managed.CloseNodeSessions()
