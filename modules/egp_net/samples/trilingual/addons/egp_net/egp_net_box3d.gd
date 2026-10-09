class_name EGPNetBox3D
extends RefCounted
## Drives an explicit EGPBox3DWorld from the server networking clock.
## SceneTree physics is independent. This adapter does not provide client rollback.
signal before_step(tick: int)
signal after_step(tick: int)
signal failed(error: Error)
var _net: Node
var _world: RefCounted
var _tracked: Dictionary = {}

func attach(net: Node, world: RefCounted) -> Error:
	if _net != null or net == null or world == null or not world.is_class("EGPBox3DWorld"):
		return ERR_INVALID_PARAMETER
	var fingerprint: String = world.get_simulation_fingerprint()
	if net.get_simulation_fingerprint() != fingerprint or not fingerprint.contains(":hz%d:" % net.get_tick_rate()):
		return ERR_INVALID_PARAMETER
	_net = net
	_world = world
	_net.simulation_tick.connect(_tick)
	return OK

## A zero body_id uses entity. Explicit IDs survive network handle replacement.
func track(entity: int, body_id: int = 0) -> Error:
	if _net == null or body_id < 0 or not _net.is_server() or _net.get_entity(entity).is_empty():
		return ERR_INVALID_PARAMETER
	_tracked[entity] = entity if body_id == 0 else body_id
	return OK

func untrack(entity: int) -> void:
	_tracked.erase(entity)

func detach() -> void:
	if is_instance_valid(_net) and _net.simulation_tick.is_connected(_tick):
		_net.simulation_tick.disconnect(_tick)
	_net = null
	_world = null
	_tracked.clear()

func _tick(tick: int, server: bool) -> void:
	if not server or not is_instance_valid(_net):
		return
	before_step.emit(tick)
	if _world == null or not is_instance_valid(_net):
		return
	if _world.get_simulation_fingerprint() != _net.get_simulation_fingerprint():
		var active := _net
		failed.emit(ERR_INVALID_PARAMETER)
		if is_instance_valid(active):
			active.stop()
		return
	var error: Error = _world.step_tick(_world.get_tick() + 1)
	if error != OK:
		var active := _net
		failed.emit(error)
		if is_instance_valid(active):
			active.stop()
		return
	for entity in _tracked.keys():
		var record: Dictionary = _net.get_entity(entity)
		if record.is_empty():
			_tracked.erase(entity)
			continue
		var body: Dictionary = _world.get_body_state(_tracked[entity])
		if body.is_empty():
			continue
		var state: Dictionary = record.state.duplicate(true)
		state["position"] = body.position
		state["rotation"] = body.rotation
		state["linear_velocity"] = body.linear_velocity
		state["angular_velocity"] = body.angular_velocity
		state["transform"] = Transform3D(Basis(body.rotation), body.position)
		state["physics_tick"] = _world.get_tick()
		error = _net.update_entity(entity, state)
		if error != OK:
			var active := _net
			failed.emit(error)
			if is_instance_valid(active):
				active.stop()
			return
	after_step.emit(tick)
