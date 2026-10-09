class_name EGPNetEntity2D
extends Node2D
## Presentation helper; simulation remains in the fixed tick loop.
signal state_applied(state: Dictionary)
@export var smoothing_speed := 15.0
var network_state: Dictionary = {}
var _target: Transform2D
var _has_target := false

func apply_network_state(state: Dictionary) -> void:
	network_state = state.duplicate(true)
	if state.get("transform") is Transform2D:
		_target = state.transform
		if not _has_target or smoothing_speed <= 0:
			global_transform = _target
		_has_target = true
	state_applied.emit(network_state.duplicate(true))

func _process(delta: float) -> void:
	if _has_target:
		global_transform = global_transform.interpolate_with(_target, 1.0 - exp(-maxf(smoothing_speed, 0.0) * delta)) if smoothing_speed > 0 else _target
