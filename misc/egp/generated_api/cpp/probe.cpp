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
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/superposition.hpp>
#include <godot_cpp/classes/superposition_config.hpp>
#include <godot_cpp/classes/superposition_prediction.hpp>
#include <godot_cpp/classes/superposition_property.hpp>
#include <godot_cpp/classes/superposition_rpc.hpp>
#include <godot_cpp/classes/superposition_rpc_method.hpp>
#include <godot_cpp/classes/superposition_scene.hpp>
#include <godot_cpp/classes/superposition_spawner.hpp>
#include <godot_cpp/classes/superposition_world.hpp>
#include <godot_cpp/core/memory.hpp>
using namespace godot;

void verify_prediction_api(const Callable &capture, const Callable &restore, const Callable &simulate, const Callable &state_hash) {
	Ref<SuperpositionPrediction> prediction;
	prediction.instantiate();
	const Error configured = prediction->configure(capture, restore, simulate, state_hash, 0, 128, 1048576, 33554432, 1);
	PackedByteArray input;
	input.resize(6);
	const Error predicted = prediction->predict(1, input);
	const Error reconciled = prediction->accept(Array(), 1);
	const Error reset = prediction->reset(2);
	const int64_t tick = prediction->get_tick();
	const int64_t acknowledged = prediction->get_acknowledged_tick();
	const String hash = prediction->get_acknowledged_hash();
	const int64_t pending = prediction->get_pending_ticks();
	const int64_t bytes = prediction->get_history_bytes();
	const Dictionary statistics = prediction->get_statistics();
	(void)configured;
	(void)predicted;
	(void)reconciled;
	(void)reset;
	(void)tick;
	(void)acknowledged;
	(void)hash;
	(void)pending;
	(void)bytes;
	(void)statistics;
}

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
	config->set_delta_replication(true);
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
	const bool reset_accepted = interpolation->submit(1, 2, Transform3D(), Vector3(), 1, Vector3(0, 1, 0));
	const double clock = interpolation->advance(1.0 / 60.0);
	const Transform3D pose = interpolation->sample(1);
	const Dictionary presentation = interpolation->get_statistics();
	interpolation->remove(1);
	interpolation->clear();
	SuperpositionWorld *world = memnew(SuperpositionWorld);
	const Error world_configured = world->configure();
	const Ref<EGPNetSession> world_session = world->get_session();
	Ref<SuperpositionScene> prefab;
	prefab.instantiate();
	prefab->set_prefab_id(1);
	Ref<PackedScene> scene;
	scene.instantiate();
	prefab->set_scene(scene);
	TypedArray<SuperpositionScene> scenes;
	scenes.push_back(prefab);
	SuperpositionSpawner *spawner = memnew(SuperpositionSpawner);
	spawner->set_scenes(scenes);
	spawner->set_session(session);
	const int64_t entity = spawner->spawn(1);
	Node *spawned = spawner->get_spawned_node(entity);
	const int64_t owner = spawner->get_owner_client_id(entity);
	const Error visibility = spawner->set_visible(entity, 1, true);
	Ref<SuperpositionRPCMethod> rule;
	rule.instantiate();
	rule->set_method_id(1);
	rule->set_method("activate");
	rule->set_permission(SuperpositionRPCMethod::OWNER);
	PackedInt32Array argument_types;
	argument_types.push_back(Variant::BOOL);
	rule->set_argument_types(argument_types);
	TypedArray<SuperpositionRPCMethod> methods;
	methods.push_back(rule);
	SuperpositionRPC *rpc = memnew(SuperpositionRPC);
	rpc->set_spawner_path(NodePath("../Spawner"));
	rpc->set_methods(methods);
	Array arguments;
	arguments.push_back(true);
	const Error called = rpc->send_rpc(entity, 1, arguments);
	const String rpc_schema = rpc->get_method_fingerprint();
	const Error despawned = spawner->despawn(entity);
	world->stop();
	memdelete(world);
	memdelete(spawner);
	memdelete(rpc);
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
	(void)reset_accepted;
	(void)world_configured;
	(void)world_session;
	(void)spawned;
	(void)owner;
	(void)visibility;
	(void)called;
	(void)rpc_schema;
	(void)despawned;
}
