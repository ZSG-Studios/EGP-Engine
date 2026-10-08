/**************************************************************************/
/*  superposition_prediction.h                                            */
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

#pragma once

#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"

#include <map>

// A complete-world local replay journal. Network frames contain inputs/hashes;
// only snapshots captured by this instance may be passed to restore_callback.
class SuperpositionPrediction : public RefCounted {
	GDCLASS(SuperpositionPrediction, RefCounted);
	struct Record {
		PackedByteArray state;
		PackedByteArray input;
		String hash;
	};
	Thread::ID owner_thread = Thread::get_caller_id();
	Callable capture_callback, restore_callback, simulate_callback, hash_callback;
	std::map<int64_t, Record> records;
	PackedByteArray baseline;
	int64_t acknowledged_tick = 0, current_tick = 0, epoch = 0, bytes = 0;
	int64_t max_ticks = 128, max_state_bytes = 1048576, max_bytes = 33554432;
	int64_t corrections = 0, replayed_ticks = 0, hash_failures = 0;
	String acknowledged_hash;
	Error last_error = OK;
	bool ready = false, busy = false;
	Error step(int64_t p_tick, const PackedByteArray &p_input, bool p_replay);
	Error fail(Error p_error);
	Error capture_baseline(int64_t p_tick);

protected:
	static void _bind_methods();

public:
	Error configure(const Callable &p_capture, const Callable &p_restore, const Callable &p_simulate, const Callable &p_hash, int64_t p_initial_tick = 0, int64_t p_max_ticks = 128, int64_t p_max_state_bytes = 1048576, int64_t p_max_bytes = 33554432, int64_t p_epoch = 0);
	Error predict(int64_t p_tick, const PackedByteArray &p_input);
	Error accept(const Array &p_frames, int64_t p_epoch = -1);
	Error reset(int64_t p_epoch, int64_t p_initial_tick = 0);
	int64_t get_tick() const;
	int64_t get_acknowledged_tick() const;
	String get_acknowledged_hash() const;
	int64_t get_pending_ticks() const;
	int64_t get_history_bytes() const;
	Dictionary get_statistics() const;
};
