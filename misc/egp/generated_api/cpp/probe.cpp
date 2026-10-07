/**************************************************************************/
/*  probe.cpp                                                             */
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

#include "modules/egp_net/samples/trilingual/extension/superposition_usage.hpp"

#include <godot_cpp/classes/egp_net_session.hpp>
#include <godot_cpp/classes/egp_net_snapshot_interpolator.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/superposition.hpp>
#include <godot_cpp/classes/superposition_config.hpp>
#include <godot_cpp/classes/superposition_property.hpp>
#include <godot_cpp/core/memory.hpp>
using namespace godot;

// Compile-only direct generated API coverage. No dynamic ClassDB calls.
void verify_generated_api(Node3D *target, const Ref<EGPNetSession> &session) {
	Ref<SuperpositionProperty> property;
	property.instantiate();
	property->set_enabled(true);
	property->set_property("position");
	property->set_value_type(Variant::VECTOR3);
	property->set_quantization(0.01);
	property->set_smoothing(true);
	TypedArray<SuperpositionProperty> rules;
	rules.push_back(property);
	Ref<SuperpositionConfig> config;
	config.instantiate();
	config->set_properties(rules);
	config->set_update_rate(20.0);
	config->set_interest_radius(64.0);
	config->set_interest_hysteresis(2.0);
	config->set_priority(2);
	config->set_capture_mode(1);
	Superposition *replication = memnew(Superposition);
	replication->set_enabled(true);
	replication->set_target_path(NodePath(".."));
	replication->set_session_path(NodePath());
	replication->set_config(config);
	replication->set_replication_key("probe");
	replication->set_entity_kind(32001);
	target->add_child(replication);
	replication->set_session(session);
	replication->mark_dirty();
	const Ref<EGPNetSession> bound = replication->get_session();
	const PackedByteArray state = replication->capture_state();
	const Error applied = replication->apply_state(state);
	const Error sent = replication->replicate_now();
	const Error observer = replication->set_observer_position(1, Vector3());
	replication->clear_observer(1);
	const Dictionary statistics = replication->get_statistics();
	Ref<EGPNetSnapshotInterpolator> interpolation;
	interpolation.instantiate();
	const Error configured = interpolation->configure(60.0, 0.2, 0.1);
	const bool accepted = interpolation->submit(1, 1, Transform3D(), Vector3());
	const double clock = interpolation->advance(1.0 / 60.0);
	const Transform3D pose = interpolation->sample(1);
	const Dictionary presentation = interpolation->get_statistics();
	interpolation->remove(1);
	interpolation->clear();
	replication->queue_free();
	(void)bound;
	(void)applied;
	(void)sent;
	(void)observer;
	(void)statistics;
	(void)configured;
	(void)accepted;
	(void)clock;
	(void)pose;
	(void)presentation;
}
