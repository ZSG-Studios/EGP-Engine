class_name EGPNetDeterministicReplay
extends RefCounted
## Canonical input replay. Solver snapshots are captured locally and never received from peers.
signal corrected(first_tick: int, replayed_ticks: int)
signal resync_required(error: Error)
var _capture: Callable
var _restore: Callable
var _simulate: Callable
var _hash: Callable
var _records: Dictionary = {}
var _baseline := PackedByteArray()
var _ack := 0
var _ack_hash := ""
var _tick := 0
var _bytes := 0
var _max_ticks := 128
var _max_state_bytes := 1048576
var _max_bytes := 33554432
var _ready := false
var _busy := false

func configure(capture: Callable, restore: Callable, simulate: Callable, state_hash: Callable, initial_tick: int = 0, max_ticks: int = 128, max_state_bytes: int = 1048576, max_bytes: int = 33554432) -> Error:
	if _ready or _busy: return ERR_ALREADY_IN_USE
	if not capture.is_valid() or not restore.is_valid() or not simulate.is_valid() or not state_hash.is_valid() or initial_tick < 0 or max_ticks < 1 or max_ticks > 512 or max_state_bytes < 1 or max_state_bytes > 1048576 or max_bytes < max_state_bytes or max_bytes > 67108864: return ERR_INVALID_PARAMETER
	_capture = capture
	_restore = restore
	_simulate = simulate
	_hash = state_hash
	_max_ticks = max_ticks
	_max_state_bytes = max_state_bytes
	_max_bytes = max_bytes
	var state: Variant = _capture.call()
	if not state is PackedByteArray or state.is_empty() or state.size() > _max_state_bytes: return ERR_INVALID_DATA
	_baseline = state.duplicate()
	_ack = initial_tick
	_tick = initial_tick
	_bytes = state.size()
	_ready = true
	return OK

func predict(tick: int, input: PackedByteArray) -> Error:
	if not _ready: return ERR_UNCONFIGURED
	if _busy: return ERR_BUSY
	if tick != _tick + 1 or tick < 1 or input.size() > 4096: return ERR_INVALID_PARAMETER
	if _records.size() >= _max_ticks or _bytes + _max_state_bytes + input.size() > _max_bytes: return ERR_BUSY
	_busy = true
	var error := _step(tick, input, false)
	_busy = false
	return error

## Frames must be contiguous after the last acknowledged tick. Each has tick, input and hash.
## Validate the entire batch before changing history. A hash mismatch fails closed.
func accept(frames: Array) -> Error:
	if not _ready: return ERR_UNCONFIGURED
	if _busy: return ERR_BUSY
	if frames.is_empty() or frames.size() > _max_ticks: return ERR_INVALID_PARAMETER
	var fresh: Array[Dictionary] = []
	var expected := _ack + 1
	for frame in frames:
		if not frame is Dictionary or not frame.get("tick") is int or not frame.get("input") is PackedByteArray or not frame.get("hash") is String: return ERR_INVALID_DATA
		if frame.tick < 1 or frame.input.size() > 4096 or frame.hash.length() != 16 or not frame.hash.is_valid_hex_number(): return ERR_INVALID_DATA
		if frame.tick <= _ack: continue
		if frame.tick != expected: return ERR_INVALID_DATA
		fresh.append({"tick": frame.tick, "input": frame.input.duplicate(), "hash": frame.hash})
		expected += 1
	if fresh.is_empty(): return OK
	if fresh.back().tick - _ack > _max_ticks: return ERR_BUSY
	var first_change := 0
	for frame in fresh:
		if _records.has(frame.tick) and _records[frame.tick].input != frame.input:
			first_change = frame.tick
			break
	_busy = true
	var replayed := 0
	if first_change > 0:
		var previous: PackedByteArray = _baseline if first_change == _ack + 1 else _records[first_change - 1].state
		var restored: Variant = _restore.call(previous.duplicate())
		if not restored is int or restored != OK: return _fail(restored if restored is int else ERR_INVALID_DATA)
		var last_predicted := _tick
		var inputs: Dictionary = {}
		for frame in fresh: inputs[frame.tick] = frame.input
		for tick in range(first_change, last_predicted + 1):
			var input: PackedByteArray = inputs.get(tick, _records[tick].input)
			var error := _step(tick, input, true)
			if error != OK: return error
			replayed += 1
	for frame in fresh:
		if not _records.has(frame.tick):
			var error := _step(frame.tick, frame.input, false)
			if error != OK: return error
		if _records[frame.tick].hash != frame.hash: return _fail(ERR_INVALID_DATA)
		var record: Dictionary = _records[frame.tick]
		_bytes -= _baseline.size()
		_baseline = record.state
		_bytes -= record.input.size()
		_records.erase(frame.tick)
		_ack = frame.tick
		_ack_hash = frame.hash
	_busy = false
	if first_change > 0: corrected.emit(first_change, replayed)
	return OK

func get_tick() -> int: return _tick
func get_acknowledged_tick() -> int: return _ack
func get_acknowledged_hash() -> String: return _ack_hash
func get_pending_ticks() -> int: return _records.size()
func get_history_bytes() -> int: return _bytes

func _step(tick: int, input: PackedByteArray, replay: bool) -> Error:
	var result: Variant = _simulate.call(tick, input.duplicate(), replay)
	if not result is int or result != OK: return _fail(result if result is int else ERR_INVALID_DATA)
	var state: Variant = _capture.call()
	var digest: Variant = _hash.call()
	if not state is PackedByteArray or state.is_empty() or state.size() > _max_state_bytes or not digest is String or digest.length() != 16: return _fail(ERR_INVALID_DATA)
	if _records.has(tick): _bytes -= _records[tick].state.size() + _records[tick].input.size()
	_bytes += state.size() + input.size()
	if _bytes > _max_bytes: return _fail(ERR_OUT_OF_MEMORY)
	_records[tick] = {"input": input.duplicate(), "state": state.duplicate(), "hash": digest}
	_tick = tick
	return OK

func _fail(error: Error) -> Error:
	_ready = false
	_busy = false
	_records.clear()
	_baseline.clear()
	_bytes = 0
	resync_required.emit(error)
	return error
