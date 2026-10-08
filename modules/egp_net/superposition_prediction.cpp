/**************************************************************************/
/*  superposition_prediction.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "superposition_prediction.h"

#include "core/object/class_db.h"

#include <vector>

namespace {
constexpr int64_t MAX_EXACT_TICK = 9007199254740991LL;
Error callback_error(const Variant &p_value) {
	if (p_value.get_type() != Variant::INT || int64_t(p_value) < 0 || int64_t(p_value) >= ERR_MAX) {
		return ERR_INVALID_DATA;
	}
	return Error(int64_t(p_value));
}
} //namespace

void SuperpositionPrediction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "capture", "restore", "simulate", "state_hash", "initial_tick", "max_ticks", "max_state_bytes", "max_bytes", "epoch"), &SuperpositionPrediction::configure, DEFVAL(0), DEFVAL(128), DEFVAL(1048576), DEFVAL(33554432), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("predict", "tick", "input"), &SuperpositionPrediction::predict);
	ClassDB::bind_method(D_METHOD("accept", "frames", "epoch"), &SuperpositionPrediction::accept, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("reset", "epoch", "initial_tick"), &SuperpositionPrediction::reset, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_tick"), &SuperpositionPrediction::get_tick);
	ClassDB::bind_method(D_METHOD("get_acknowledged_tick"), &SuperpositionPrediction::get_acknowledged_tick);
	ClassDB::bind_method(D_METHOD("get_acknowledged_hash"), &SuperpositionPrediction::get_acknowledged_hash);
	ClassDB::bind_method(D_METHOD("get_pending_ticks"), &SuperpositionPrediction::get_pending_ticks);
	ClassDB::bind_method(D_METHOD("get_history_bytes"), &SuperpositionPrediction::get_history_bytes);
	ClassDB::bind_method(D_METHOD("get_statistics"), &SuperpositionPrediction::get_statistics);
	ADD_SIGNAL(MethodInfo("corrected", PropertyInfo(Variant::INT, "first_tick"), PropertyInfo(Variant::INT, "replayed_ticks")));
	ADD_SIGNAL(MethodInfo("resync_required", PropertyInfo(Variant::INT, "error")));
}

Error SuperpositionPrediction::capture_baseline(int64_t p_tick) {
	Variant state = capture_callback.callv(Array());
	Variant digest = hash_callback.callv(Array());
	if (state.get_type() != Variant::PACKED_BYTE_ARRAY || digest.get_type() != Variant::STRING) {
		return fail(ERR_INVALID_DATA);
	}
	PackedByteArray captured = state;
	String hash = digest;
	if (captured.is_empty() || captured.size() > max_state_bytes || hash.length() != 16 || !hash.is_valid_hex_number(false) || hash[0] == '-' || hash[0] == '+') {
		return fail(ERR_INVALID_DATA);
	}
	baseline = captured;
	bytes = baseline.size();
	acknowledged_tick = current_tick = p_tick;
	acknowledged_hash = hash;
	ready = true;
	busy = false;
	last_error = OK;
	return OK;
}

Error SuperpositionPrediction::configure(const Callable &p_capture, const Callable &p_restore, const Callable &p_simulate, const Callable &p_hash, int64_t p_initial_tick, int64_t p_max_ticks, int64_t p_max_state_bytes, int64_t p_max_bytes, int64_t p_epoch) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	Ref<SuperpositionPrediction> keep_alive(this);
	if (ready || busy) {
		return ERR_ALREADY_IN_USE;
	}
	if (!p_capture.is_valid() || !p_restore.is_valid() || !p_simulate.is_valid() || !p_hash.is_valid() || p_initial_tick < 0 || p_initial_tick > MAX_EXACT_TICK || p_epoch < 0 || p_epoch > MAX_EXACT_TICK || p_max_ticks < 1 || p_max_ticks > 512 || p_max_state_bytes < 1 || p_max_state_bytes > 1048576 || p_max_bytes < p_max_state_bytes || p_max_bytes > 67108864) {
		return ERR_INVALID_PARAMETER;
	}
	capture_callback = p_capture;
	restore_callback = p_restore;
	simulate_callback = p_simulate;
	hash_callback = p_hash;
	max_ticks = p_max_ticks;
	max_state_bytes = p_max_state_bytes;
	max_bytes = p_max_bytes;
	epoch = p_epoch;
	records.clear();
	busy = true;
	return capture_baseline(p_initial_tick);
}

Error SuperpositionPrediction::fail(Error p_error) {
	ready = false;
	busy = true;
	records.clear();
	baseline.clear();
	bytes = 0;
	last_error = p_error;
	// Callbacks may release their last external reference, but cannot reset or
	// configure this journal until the failing operation has unwound. Defer
	// rebuilding trusted local genesis to the next frame.
	emit_signal("resync_required", p_error);
	busy = false;
	return p_error;
}

Error SuperpositionPrediction::step(int64_t p_tick, const PackedByteArray &p_input, bool p_replay) {
	Array args;
	args.push_back(p_tick);
	args.push_back(p_input);
	args.push_back(p_replay);
	Variant result = simulate_callback.callv(args);
	if (result.get_type() != Variant::INT || int64_t(result) != OK) {
		return fail(callback_error(result));
	}
	Variant state = capture_callback.callv(Array());
	Variant digest = hash_callback.callv(Array());
	if (state.get_type() != Variant::PACKED_BYTE_ARRAY || digest.get_type() != Variant::STRING) {
		return fail(ERR_INVALID_DATA);
	}
	PackedByteArray captured = state;
	String hash = digest;
	if (captured.is_empty() || captured.size() > max_state_bytes || hash.length() != 16 || !hash.is_valid_hex_number(false) || hash[0] == '-' || hash[0] == '+') {
		return fail(ERR_INVALID_DATA);
	}
	auto previous = records.find(p_tick);
	if (previous != records.end()) {
		bytes -= previous->second.state.size() + previous->second.input.size();
	}
	bytes += captured.size() + p_input.size();
	if (bytes > max_bytes) {
		return fail(ERR_OUT_OF_MEMORY);
	}
	records[p_tick] = { captured, p_input, hash };
	current_tick = p_tick;
	return OK;
}

Error SuperpositionPrediction::predict(int64_t p_tick, const PackedByteArray &p_input) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	Ref<SuperpositionPrediction> keep_alive(this);
	if (!ready) {
		return ERR_UNCONFIGURED;
	}
	if (busy) {
		return ERR_BUSY;
	}
	if (p_tick < 1 || p_tick > MAX_EXACT_TICK || p_tick != current_tick + 1 || p_input.size() > 4096) {
		return ERR_INVALID_PARAMETER;
	}
	// Refuse capacity before invoking game code, so history pressure cannot
	// silently advance an unjournaled solver tick.
	if (records.size() >= size_t(max_ticks) || bytes + max_state_bytes + p_input.size() > max_bytes) {
		return ERR_BUSY;
	}
	busy = true;
	Error result = step(p_tick, p_input, false);
	busy = false;
	return result;
}

Error SuperpositionPrediction::accept(const Array &p_frames, int64_t p_epoch) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	Ref<SuperpositionPrediction> keep_alive(this);
	if (!ready) {
		return ERR_UNCONFIGURED;
	}
	if (busy) {
		return ERR_BUSY;
	}
	if ((p_epoch != -1 && p_epoch != epoch) || p_frames.is_empty() || p_frames.size() > max_ticks) {
		return ERR_INVALID_PARAMETER;
	}
	struct Frame {
		int64_t tick;
		PackedByteArray input;
		String hash;
	};
	std::vector<Frame> fresh;
	int64_t expected = acknowledged_tick + 1;
	for (int i = 0; i < p_frames.size(); ++i) {
		if (p_frames[i].get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		Dictionary frame = p_frames[i];
		Variant tick_value = frame.get("tick", Variant());
		Variant input_value = frame.get("input", Variant());
		Variant hash_value = frame.get("hash", Variant());
		if (tick_value.get_type() != Variant::INT || input_value.get_type() != Variant::PACKED_BYTE_ARRAY || hash_value.get_type() != Variant::STRING) {
			return ERR_INVALID_DATA;
		}
		int64_t tick = tick_value;
		PackedByteArray input = input_value;
		String hash = hash_value;
		if (tick < 1 || tick > MAX_EXACT_TICK || input.size() > 4096 || hash.length() != 16 || !hash.is_valid_hex_number(false) || hash[0] == '-' || hash[0] == '+') {
			return ERR_INVALID_DATA;
		}
		if (tick <= acknowledged_tick) {
			continue;
		}
		if (tick != expected++) {
			return ERR_INVALID_DATA;
		}
		fresh.push_back({ tick, input, hash });
	}
	if (fresh.empty()) {
		return OK;
	}
	int64_t first_change = 0;
	std::map<int64_t, PackedByteArray> canonical_inputs;
	for (const Frame &frame : fresh) {
		canonical_inputs[frame.tick] = frame.input;
		auto record = records.find(frame.tick);
		if (!first_change && record != records.end() && record->second.input != frame.input) {
			first_change = frame.tick;
		}
	}
	busy = true;
	int64_t replayed = 0;
	if (first_change) {
		PackedByteArray previous = first_change == acknowledged_tick + 1 ? baseline : records.at(first_change - 1).state;
		Array args;
		args.push_back(previous);
		Variant restored = restore_callback.callv(args);
		if (restored.get_type() != Variant::INT || int64_t(restored) != OK) {
			return fail(callback_error(restored));
		}
		int64_t last_predicted = current_tick;
		for (int64_t tick = first_change; tick <= last_predicted; ++tick) {
			auto replacement = canonical_inputs.find(tick);
			PackedByteArray input = replacement == canonical_inputs.end() ? records.at(tick).input : replacement->second;
			Error result = step(tick, input, true);
			if (result != OK) {
				return result;
			}
			++replayed;
		}
	}
	for (const Frame &frame : fresh) {
		if (records.find(frame.tick) == records.end()) {
			Error result = step(frame.tick, frame.input, false);
			if (result != OK) {
				return result;
			}
		}
		const Record &record = records.at(frame.tick);
		if (record.hash != frame.hash) {
			++hash_failures;
			return fail(ERR_INVALID_DATA);
		}
		bytes -= baseline.size();
		baseline = record.state;
		bytes -= record.input.size();
		records.erase(frame.tick);
		acknowledged_tick = frame.tick;
		acknowledged_hash = frame.hash;
	}
	if (first_change) {
		++corrections;
		replayed_ticks += replayed;
		emit_signal("corrected", first_change, replayed);
	}
	busy = false;
	return OK;
}

Error SuperpositionPrediction::reset(int64_t p_epoch, int64_t p_initial_tick) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	Ref<SuperpositionPrediction> keep_alive(this);
	if (busy) {
		return ERR_BUSY;
	}
	if (p_epoch <= epoch || p_epoch > MAX_EXACT_TICK || p_initial_tick < 0 || p_initial_tick > MAX_EXACT_TICK || !capture_callback.is_valid()) {
		return ERR_INVALID_PARAMETER;
	}
	ready = false;
	busy = true;
	records.clear();
	epoch = p_epoch;
	return capture_baseline(p_initial_tick);
}

int64_t SuperpositionPrediction::get_tick() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return current_tick;
}
int64_t SuperpositionPrediction::get_acknowledged_tick() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return acknowledged_tick;
}
String SuperpositionPrediction::get_acknowledged_hash() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, String());
	return acknowledged_hash;
}
int64_t SuperpositionPrediction::get_pending_ticks() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return records.size();
}
int64_t SuperpositionPrediction::get_history_bytes() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return bytes;
}
Dictionary SuperpositionPrediction::get_statistics() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result;
	result["ready"] = ready;
	result["busy"] = busy;
	result["epoch"] = epoch;
	result["tick"] = current_tick;
	result["acknowledged_tick"] = acknowledged_tick;
	result["pending_ticks"] = int64_t(records.size());
	result["history_bytes"] = bytes;
	result["corrections"] = corrections;
	result["replayed_ticks"] = replayed_ticks;
	result["hash_failures"] = hash_failures;
	result["last_error"] = last_error;
	return result;
}
