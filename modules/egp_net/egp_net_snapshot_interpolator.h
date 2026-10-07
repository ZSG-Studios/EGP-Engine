/**************************************************************************/
/*  egp_net_snapshot_interpolator.h                                       */
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
#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

#include <deque>
#include <map>

// Presentation only. Never modifies authoritative or predicted physics state.
class EGPNetSnapshotInterpolator : public RefCounted {
	GDCLASS(EGPNetSnapshotInterpolator, RefCounted);
	struct Snapshot {
		int64_t tick;
		Transform3D pose;
		Vector3 velocity;
	};
	struct Track {
		std::deque<Snapshot> samples;
		Vector3 correction;
		Quaternion rotation_correction;
		int64_t interpolated = 0, extrapolated = 0, held = 0, priming = 0, corrections = 0;
		double largest_correction = 0.0;
		double largest_angular_correction = 0.0;
	};
	Thread::ID owner_thread = Thread::get_caller_id();
	std::map<int64_t, Track> tracks;
	double tick_rate = 60.0;
	double delay_ticks = 12.0;
	double extrapolation_ticks = 6.0;
	double clock = 0.0;
	double newest_tick = 0.0;
	double since_arrival = 0.0;
	bool started = false;
	int64_t interpolated = 0, extrapolated = 0, held = 0, corrections = 0, teleports = 0, priming = 0, clock_holds = 0;
	double largest_correction = 0.0;
	double largest_angular_correction = 0.0;
	Transform3D evaluate(const Track &p_track, double p_tick) const;

protected:
	static void _bind_methods();

public:
	Error configure(double p_tick_rate = 60.0, double p_delay_seconds = 0.2, double p_max_extrapolation_seconds = 0.1);
	bool submit(int64_t p_entity, int64_t p_tick, const Transform3D &p_pose, const Vector3 &p_velocity);
	double advance(double p_delta);
	Transform3D sample(int64_t p_entity, double p_delta = 0.0);
	Dictionary get_statistics() const;
	void remove(int64_t p_entity);
	void clear();
};
