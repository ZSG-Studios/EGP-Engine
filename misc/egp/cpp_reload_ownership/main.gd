extends Node
var probe: Node
var assertions := 0
var phases: Array = []
var transfers: Array = []
var faults: Array = []
var native_recovery := false

func check(value: bool, message: String) -> void:
	assertions += 1
	if not value:
		push_error("EGP_CPP_OWNERSHIP_FAILED " + message)
		get_tree().quit(1)
		assert(value, message)

func snapshot() -> Dictionary:
	var proof: Dictionary = probe.proof()
	proof.erase("world")
	return proof

func pump(min_tick: int, message_count: int) -> void:
	for frame in 600:
		check(probe.poll() == OK, "poll failed")
		var proof := snapshot()
		var record: Dictionary = proof.client_entity
		var state: Dictionary = record.get("state", {})
		if proof.world_tick >= min_tick and state.get("physics_tick", 0) >= min_tick and proof.get("messages", 0) == message_count:
			return
		await get_tree().process_frame
	check(false, "authenticated baseline/tick/message timeout")

func validate(proof: Dictionary, version: int, previous: Dictionary = {}) -> void:
	check(proof.version == version and proof.server_state == "Listening" and proof.client_state == "Connected", "version/session state")
	for name in ["server", "client", "adapter", "server_session", "client_session"]:
		check(proof[name + "_id"] == proof[name + "_live_id"], "retained " + name)
	check(proof.before == proof.after and proof.after == proof.world_tick and proof.get("failures", 0) == 0, "exact callback/physics clock count")
	check(proof.before_connections == 1 and proof.clock_connections == 1, "single owned callback and adapter clock")
	check(proof.get("unexpected_tick_callback", false) == false, "explicit disconnect removes owned callback")
	check(proof.body_map.size() == 1 and proof.body_map[proof.entity] == 10000, "stable entity/body mapping")
	check(proof.client_entity.state.physics_tick > 0 and proof.body.position.y < 10000, "replicated moving authoritative body")
	check(proof.capsules == 0, "capsules consumed")
	if not previous.is_empty():
		check(proof.world_id == previous.world_id and proof.entity == previous.entity, "world/entity identity")
		check(proof.world_tick > previous.world_tick and proof.body.position.y < previous.body.position.y, "physics advances after reload")
		check(proof.handoffs == version - 1 and proof.restores == version - 1 and proof.checks >= 30 * (version - 1), "ownership controls and transfers")
	phases.append(proof)

func descriptor_library(library: String) -> void:
	var file := FileAccess.open("res://ownership.gdextension", FileAccess.WRITE)
	check(file != null, "descriptor open")
	file.store_string('[configuration]\nentry_symbol="ownership_init"\ncompatibility_minimum="4.8"\nreloadable=true\n[libraries]\nwindows.debug.x86_64="res://bin/%s.dll"\n' % library)
	file.close()

func inject_fault(kind: String, version: int, node_id: int) -> void:
	descriptor_library("ownership-" + kind)
	var status := GDExtensionManager.reload_extension("res://ownership.gdextension")
	check(status == GDExtensionManager.LOAD_STATUS_FAILED, "injected library load must fail")
	check(probe.get_instance_id() == node_id and probe.get_class() == "Node", "failed load retains native parent")
	check(not probe.has_method("resume") and not probe.has_method("proof"), "failed library has no callable extension methods")
	check(probe.get_child_count() == 2, "network bridge children retained during failure")
	check(not GDExtensionManager.get_extension("res://ownership.gdextension").is_library_open(), "failed library stays closed")
	probe.name = "RecoveredOwnership%d%s" % [version, kind]
	faults.append({"kind": kind, "version": version, "status": status, "node_id": node_id, "parent_id": probe.get_parent().get_instance_id(), "name": str(probe.name), "children": probe.get_child_count(), "methods_unavailable": not probe.has_method("resume"), "library_closed": not GDExtensionManager.get_extension("res://ownership.gdextension").is_library_open()})

func _ready() -> void:
	native_recovery = "--native-recovery" in OS.get_cmdline_user_args()
	probe = ClassDB.instantiate("EGPNetOwnershipProbe")
	add_child(probe)
	check(probe.start() == OK, "start")
	# Reliable traffic is sent only after authenticated admission has a complete baseline.
	await pump(8, 0)
	check(probe.send() == OK, "initial message")
	await pump(12, 1)
	var previous := snapshot()
	validate(previous, 1)
	for version in [2, 3]:
		var node_id := probe.get_instance_id()
		check(probe.suspend() == OK, "explicit owner handoff")
		var frozen := snapshot()
		var fault_start := Time.get_ticks_msec()
		if native_recovery:
			inject_fault("missing", version, node_id)
			inject_fault("invalid", version, node_id)
		descriptor_library("ownership%d" % version)
		check(GDExtensionManager.reload_extension("res://ownership.gdextension") == 0, "real compatible DLL reload")
		check(probe.get_instance_id() == node_id and probe.proof().version == version, "same live extension node/new code")
		var fault_elapsed_ms := Time.get_ticks_msec() - fault_start
		if native_recovery:
			check(str(probe.name) == "RecoveredOwnership%dinvalid" % version, "parent-property edit persists through consecutive failures")
			check(fault_elapsed_ms < 500, "fault interval exceeded qualified clock catch-up budget")
		check(probe.resume() == OK, "consume serialized native owner capsules")
		var restored := snapshot()
		check(restored.world_tick == frozen.world_tick and restored.world_hash == frozen.world_hash, "exact solver state across unload")
		transfers.append({"version": version, "node_id": node_id, "restored_node_id": probe.get_instance_id(), "tick": frozen.world_tick, "restored_tick": restored.world_tick, "hash": frozen.world_hash, "restored_hash": restored.world_hash, "fault_elapsed_ms": fault_elapsed_ms, "parent_name": str(probe.name)})
		check(probe.send() == OK, "message handler re-registration")
		await pump(previous.world_tick + 8, version)
		var proof := snapshot()
		validate(proof, version, previous)
		previous = proof
	var report := {"passed": true, "assertions": assertions, "phases": phases, "transfers": transfers, "native_recovery": native_recovery, "faults": faults}
	print("EGP_CPP_OWNERSHIP_PASSED " + JSON.stringify(report))
	get_tree().quit(0)
