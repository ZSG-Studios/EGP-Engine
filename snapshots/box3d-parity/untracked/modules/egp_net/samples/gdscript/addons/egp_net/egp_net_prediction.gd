class_name EGPNetPrediction
extends RefCounted
## Bounded prediction history with authoritative correction and deterministic input replay.
## Capture/restore/simulate callbacks define the game's complete predicted state.
signal corrected(acknowledged_tick: int, replayed_inputs: int)
signal resync_required(error: Error)
var _capture: Callable
var _restore: Callable
var _simulate: Callable
var _history: Array[Dictionary] = []
var _last_tick := 0
var _acknowledged_tick := 0
var _max_ticks := 128
var _max_state_bytes := 65536
var _max_history_bytes := 8388608
var _history_bytes := 0
var _busy := false
var _ready := false

func configure(capture: Callable, restore: Callable, simulate: Callable, initial_tick: int = 0, max_ticks: int = 128, max_state_bytes: int = 65536, max_history_bytes: int = 8388608) -> Error:
	if _ready or _busy:
		return ERR_ALREADY_IN_USE
	if not capture.is_valid() or not restore.is_valid() or not simulate.is_valid() or initial_tick < 0 or initial_tick > 0x7ffffffffffffffe or max_ticks < 1 or max_ticks > 512 or max_state_bytes < 1 or max_state_bytes > 1048576 or max_history_bytes < max_state_bytes or max_history_bytes > 67108864:
		return ERR_INVALID_PARAMETER
	_capture = capture
	_restore = restore
	_simulate = simulate
	_last_tick = initial_tick
	_acknowledged_tick = initial_tick
	_max_ticks = max_ticks
	_max_state_bytes = max_state_bytes
	_max_history_bytes = max_history_bytes
	_ready = true
	return OK

## Caller sends the same tick/input through its ownership-checked network input handler.
## simulate(tick, input, replay) returns Error; capture() returns PackedByteArray.
func predict(tick: int, input: PackedByteArray) -> Error:
	if not _ready:
		return ERR_UNCONFIGURED
	if _busy:
		return ERR_BUSY
	if tick < 1 or tick != _last_tick + 1 or tick > 0x7ffffffffffffffe or input.size() > 4096:
		return ERR_INVALID_PARAMETER
	# Reserve the worst case before simulation; history pressure cannot silently discard input.
	if _history.size() >= _max_ticks or _history_bytes + input.size() + _max_state_bytes > _max_history_bytes:
		return ERR_BUSY
	_busy = true
	var result: Variant = _simulate.call(tick, input.duplicate(), false)
	if not result is int or result != OK:
		return _fail(result if result is int else ERR_INVALID_DATA)
	var state: Variant = _capture.call()
	if not state is PackedByteArray or state.is_empty() or state.size() > _max_state_bytes:
		return _fail(ERR_INVALID_DATA)
	_history.append({"tick": tick, "input": input.duplicate(), "state": state.duplicate()})
	_history_bytes += input.size() + state.size()
	_last_tick = tick
	_busy = false
	return OK

## Server acknowledgment uses the accepted input sequence, including late/rejected input policy.
func reconcile(acknowledged_tick: int, authoritative_state: PackedByteArray) -> Error:
	if not _ready:
		return ERR_UNCONFIGURED
	if _busy:
		return ERR_BUSY
	if acknowledged_tick <= _acknowledged_tick:
		return OK
	if acknowledged_tick > _last_tick or authoritative_state.is_empty() or authoritative_state.size() > _max_state_bytes:
		return ERR_INVALID_PARAMETER
	var index := acknowledged_tick - _acknowledged_tick - 1
	if index < 0 or index >= _history.size() or _history[index].tick != acknowledged_tick:
		return _fail(ERR_INVALID_DATA)
	_busy = true
	var mismatch: bool = _history[index].state != authoritative_state
	if mismatch:
		var result: Variant = _restore.call(authoritative_state.duplicate())
		if not result is int or result != OK:
			return _fail(result if result is int else ERR_INVALID_DATA)
		for i in range(index + 1, _history.size()):
			var record: Dictionary = _history[i]
			result = _simulate.call(record.tick, record.input.duplicate(), true)
			if not result is int or result != OK:
				return _fail(result if result is int else ERR_INVALID_DATA)
			var state: Variant = _capture.call()
			if not state is PackedByteArray or state.is_empty() or state.size() > _max_state_bytes:
				return _fail(ERR_INVALID_DATA)
			_history_bytes += state.size() - record.state.size()
			if _history_bytes > _max_history_bytes:
				return _fail(ERR_OUT_OF_MEMORY)
			record.state = state.duplicate()
	var replayed := _history.size() - index - 1
	for _i in range(index + 1):
		var record: Dictionary = _history.pop_front()
		_history_bytes -= record.input.size() + record.state.size()
	_acknowledged_tick = acknowledged_tick
	_busy = false
	if mismatch:
		corrected.emit(acknowledged_tick, replayed)
	return OK

## Restore a fresh baseline after resync or before predicting a newly owned entity.
func reset(tick: int, state: PackedByteArray) -> Error:
	if _busy or not _restore.is_valid():
		return ERR_BUSY
	if tick < 0 or tick > 0x7ffffffffffffffe or state.is_empty() or state.size() > _max_state_bytes:
		return ERR_INVALID_PARAMETER
	_busy = true
	var result: Variant = _restore.call(state.duplicate())
	if not result is int or result != OK:
		return _fail(result if result is int else ERR_INVALID_DATA)
	_history.clear()
	_history_bytes = 0
	_last_tick = tick
	_acknowledged_tick = tick
	_ready = true
	_busy = false
	return OK

func get_pending_ticks() -> int:
	return _history.size()

func get_history_bytes() -> int:
	return _history_bytes

func _fail(error: Error) -> Error:
	_ready = false
	_busy = false
	_history.clear()
	_history_bytes = 0
	resync_required.emit(error)
	return error
