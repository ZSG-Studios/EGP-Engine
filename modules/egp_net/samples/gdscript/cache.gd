extends Node
var changed := 0
func _ready() -> void:
	var net := preload("res://addons/egp_net/egp_net.gd").new()
	net.auto_poll = false
	add_child(net)
	assert(net.configure({"max_entities": 256}) == OK)
	assert(net.host(0, "127.0.0.1") == OK)
	var handles: Array[int] = []
	for index in range(256):
		var handle: int = net.spawn(1, {"value": -1, "index": index})
		assert(handle != 0)
		handles.append(handle)
	net.entity_changed.connect(func(_entity: int, _state: Dictionary): changed += 1)
	var started := Time.get_ticks_usec()
	for round_index in range(4):
		for handle in handles:
			assert(net.update_entity(handle, {"value": round_index, "index": handle}) == OK)
			assert(net.get_entity(handle).state.value == round_index)
	var update_ms := (Time.get_ticks_usec() - started) / 1000.0
	assert(changed == 1024)
	var row: Variant = net.session.command("entity", {"entity": handles[0]})
	assert(row is Dictionary and row.entity == handles[0] and row.revision == 5)
	assert(net.session.command("entity", {"entity": 0}).is_empty())
	var copy: Dictionary = net.get_entity(handles[0])
	copy.state.value = 999
	assert(net.get_entity(handles[0]).state.value == 3)
	assert(net.despawn(handles[0]) == OK and net.get_entity(handles[0]).is_empty())
	assert(net.get_entities().size() == 255)
	assert(net.update_entity(handles[0], {}) == ERR_DOES_NOT_EXIST)
	net.close()
	assert(net.get_entities().is_empty())
	print("EGP_NETWORK_CACHE " + JSON.stringify({"passed": true, "entities": 256, "updates": changed, "update_ms": update_ms, "version": Engine.get_version_info().string}))
	get_tree().quit()
