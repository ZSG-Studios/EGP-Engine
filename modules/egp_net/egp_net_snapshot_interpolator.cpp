/**************************************************************************/
/*  egp_net_snapshot_interpolator.cpp                                     */
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

#include "egp_net_snapshot_interpolator.h"

#include "core/object/class_db.h"

void EGPNetSnapshotInterpolator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "tick_rate", "delay_seconds", "max_extrapolation_seconds"), &EGPNetSnapshotInterpolator::configure, DEFVAL(60.0), DEFVAL(0.2), DEFVAL(0.1));
	ClassDB::bind_method(D_METHOD("submit", "entity", "tick", "pose", "velocity"), &EGPNetSnapshotInterpolator::submit);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &EGPNetSnapshotInterpolator::advance);
	ClassDB::bind_method(D_METHOD("sample", "entity", "delta"), &EGPNetSnapshotInterpolator::sample, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_statistics"), &EGPNetSnapshotInterpolator::get_statistics);
	ClassDB::bind_method(D_METHOD("remove", "entity"), &EGPNetSnapshotInterpolator::remove);
	ClassDB::bind_method(D_METHOD("clear"), &EGPNetSnapshotInterpolator::clear);
}

Error EGPNetSnapshotInterpolator::configure(double p_tick_rate, double p_delay_seconds, double p_max_extrapolation_seconds) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	if (!Math::is_finite(p_tick_rate) || !Math::is_finite(p_delay_seconds) || !Math::is_finite(p_max_extrapolation_seconds) || p_tick_rate < 1 || p_tick_rate > 1000 || p_delay_seconds < 0 || p_delay_seconds > 2 || p_max_extrapolation_seconds < 0 || p_max_extrapolation_seconds > 0.5) {
		return ERR_INVALID_PARAMETER;
	}
	tick_rate = p_tick_rate;
	delay_ticks = p_delay_seconds * tick_rate;
	extrapolation_ticks = p_max_extrapolation_seconds * tick_rate;
	clear();
	return OK;
}

Transform3D EGPNetSnapshotInterpolator::evaluate(const Track &p_track, double p_tick) const {
	const auto &samples = p_track.samples;
	if (p_tick <= samples.front().tick) {
		return samples.front().pose;
	}
	for (size_t i = 1; i < samples.size(); ++i) {
		if (p_tick <= samples[i].tick) {
			double weight = (p_tick - samples[i - 1].tick) / double(samples[i].tick - samples[i - 1].tick);
			return samples[i - 1].pose.interpolate_with(samples[i].pose, weight);
		}
	}
	const Snapshot &latest = samples.back();
	Transform3D result = latest.pose;
	result.origin += latest.velocity * (CLAMP(p_tick - latest.tick, 0.0, extrapolation_ticks) / tick_rate);
	return result;
}

bool EGPNetSnapshotInterpolator::submit(int64_t p_entity, int64_t p_tick, const Transform3D &p_pose, const Vector3 &p_velocity) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (p_entity <= 0 || p_tick < 0 || p_tick > 9007199254740991LL || !p_pose.is_finite() || !p_velocity.is_finite()) {
		return false;
	}
	auto found = tracks.find(p_entity);
	if (found == tracks.end() && tracks.size() >= 1024) {
		return false;
	}
	Track &track = tracks[p_entity];
	if (!track.samples.empty() && p_tick <= track.samples.back().tick) {
		return false;
	}
	bool was_extrapolating = !track.samples.empty() && clock > track.samples.back().tick;
	Transform3D before;
	if (!track.samples.empty()) {
		before = evaluate(track, clock);
		if (track.samples.back().pose.origin.distance_to(p_pose.origin) > 8.0) {
			track.samples.clear();
			track.correction = Vector3();
			track.rotation_correction = Quaternion();
			was_extrapolating = false;
			++teleports;
		}
	}
	track.samples.push_back({ p_tick, p_pose, p_velocity });
	while (track.samples.size() > 32) {
		track.samples.pop_front();
	}
	// An interest gap/loss may require extrapolation. Re-entry must not snap the
	// displayed body backward; decay its presentation offset after correction.
	if (was_extrapolating) {
		Transform3D after = evaluate(track, clock);
		Vector3 error = before.origin - after.origin;
		Quaternion rotation_error = (before.basis.get_rotation_quaternion() * after.basis.get_rotation_quaternion().inverse()).normalized();
		if (rotation_error.w < 0) {
			rotation_error = -rotation_error;
		}
		double angular_error = Quaternion().angle_to(rotation_error);
		if (error.length_squared() > 0.000001 || angular_error > 0.00001) {
			track.correction += error;
			track.rotation_correction = (track.rotation_correction * rotation_error).normalized();
			if (track.rotation_correction.w < 0) {
				track.rotation_correction = -track.rotation_correction;
			}
			largest_angular_correction = MAX(largest_angular_correction, angular_error);
			track.largest_angular_correction = MAX(track.largest_angular_correction, angular_error);
			largest_correction = MAX(largest_correction, double(error.length()));
			++corrections;
			++track.corrections;
			track.largest_correction = MAX(track.largest_correction, double(error.length()));
		}
	}
	if (!started || p_tick > newest_tick) {
		newest_tick = p_tick;
		since_arrival = 0;
		if (!started) {
			clock = p_tick - delay_ticks;
			started = true;
		}
	}
	return true;
}

double EGPNetSnapshotInterpolator::advance(double p_delta) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0.0);
	if (!started || !Math::is_finite(p_delta) || p_delta < 0) {
		return clock;
	}
	// Arrival batching must not reset the playback clock or move it backward.
	// Stop at the newest buffered tick during outages instead of letting the
	// wall clock outrun authority indefinitely. Rate correction is bounded.
	double delta = MIN(p_delta, 0.25);
	since_arrival += delta;
	double desired = newest_tick + MIN(since_arrival * tick_rate, 3.0) - delay_ticks;
	double speed = CLAMP(1.0 + (desired - clock) * 0.025, 0.9, 1.1);
	double previous = clock;
	clock = MAX(clock, MIN(clock + delta * tick_rate * speed, newest_tick));
	if (delta > 0 && clock == previous) {
		++clock_holds;
	}
	return clock;
}

Transform3D EGPNetSnapshotInterpolator::sample(int64_t p_entity, double p_delta) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Transform3D());
	if (!Math::is_finite(p_delta) || p_delta < 0) {
		return Transform3D();
	}
	auto found = tracks.find(p_entity);
	if (found == tracks.end()) {
		return Transform3D();
	}
	Track &track = found->second;
	double age = clock - track.samples.back().tick;
	if (clock < track.samples.front().tick) {
		++priming;
		++track.priming;
	} else if (age > extrapolation_ticks) {
		++held;
		++track.held;
	} else if (age > 0) {
		++extrapolated;
		++track.extrapolated;
	} else {
		++interpolated;
		++track.interpolated;
	}
	Transform3D result = evaluate(track, clock);
	if (Math::is_finite(p_delta) && p_delta > 0) {
		double decay = Math::exp(-MIN(p_delta, 0.25) * 12.0);
		track.correction *= decay;
		track.rotation_correction = track.rotation_correction.slerp(Quaternion(), 1.0 - decay).normalized();
	}
	result.origin += track.correction;
	result.basis = Basis(track.rotation_correction) * result.basis;
	return result;
}

Dictionary EGPNetSnapshotInterpolator::get_statistics() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result;
	result["render_tick"] = clock;
	result["buffer_ticks"] = newest_tick - clock;
	result["tracked_entities"] = int64_t(tracks.size());
	result["interpolated_samples"] = interpolated;
	result["extrapolated_samples"] = extrapolated;
	result["held_samples"] = held;
	result["corrections"] = corrections;
	result["teleports"] = teleports;
	result["priming_samples"] = priming;
	result["clock_hold_frames"] = clock_holds;
	result["largest_correction_m"] = largest_correction;
	result["largest_angular_correction_degrees"] = Math::rad_to_deg(largest_angular_correction);
	Dictionary entities;
	for (const auto &entry : tracks) {
		const Track &track = entry.second;
		Dictionary stats;
		stats["interpolated_samples"] = track.interpolated;
		stats["extrapolated_samples"] = track.extrapolated;
		stats["held_samples"] = track.held;
		stats["priming_samples"] = track.priming;
		stats["corrections"] = track.corrections;
		stats["largest_correction_m"] = track.largest_correction;
		stats["largest_angular_correction_degrees"] = Math::rad_to_deg(track.largest_angular_correction);
		entities[entry.first] = stats;
	}
	result["entities"] = entities;
	return result;
}

void EGPNetSnapshotInterpolator::remove(int64_t p_entity) {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	tracks.erase(p_entity);
}
void EGPNetSnapshotInterpolator::clear() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	tracks.clear();
	started = false;
	clock = newest_tick = since_arrival = 0;
	interpolated = extrapolated = held = corrections = teleports = priming = clock_holds = 0;
	largest_correction = largest_angular_correction = 0;
}
