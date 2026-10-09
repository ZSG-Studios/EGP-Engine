/**************************************************************************/
/*  test_superposition_2d.h                                               */
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

#include "../egp_net_session.h"
#include "../superposition.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

#include <limits>

namespace TestSuperposition {
#ifdef _3D_DISABLED

TEST_CASE("[SceneTree][Superposition][No3D] Generic native replication and spatial rejection") {
	CHECK_FALSE(ClassDB::class_exists("Node3D"));
	Ref<EGPNetSession> server;
	Ref<EGPNetSession> client;
	server.instantiate();
	client.instantiate();
	Dictionary options;
	options["allow_insecure_loopback"] = true;
	options["game_protocol"] = "superposition-2d-native-fixture";
	options["max_entities"] = 8;
	REQUIRE_EQ(server->configure(options), OK);
	REQUIRE_EQ(client->configure(options), OK);
	REQUIRE_EQ(server->listen(0, "127.0.0.1"), OK);

	Node *actor = memnew(Node);
	Node *copy = memnew(Node);
	SceneTree::get_singleton()->get_root()->add_child(actor);
	SceneTree::get_singleton()->get_root()->add_child(copy);
	Ref<SuperpositionConfig> config;
	config.instantiate();
	Ref<SuperpositionProperty> property;
	property.instantiate();
	property->set_property("process_priority");
	property->set_value_type(Variant::INT);
	TypedArray<SuperpositionProperty> properties;
	properties.push_back(property);
	config->set_properties(properties);
	auto component = [&](Node *target, const Ref<EGPNetSession> &session) {
		Superposition *node = memnew(Superposition);
		node->set_enabled(false);
		node->set_replication_key("generic-property");
		node->set_config(config);
		target->add_child(node);
		node->set_session(session);
		return node;
	};
	Superposition *source = component(actor, server);
	Superposition *replica = component(copy, client);
	config->set_interest_radius(5.0);
	CHECK(source->capture_state().is_empty());
	CHECK_EQ(source->replicate_now(), ERR_UNAVAILABLE);
	CHECK(Array(server->command("entities")).is_empty());
	config->set_interest_radius(std::numeric_limits<double>::infinity());
	CHECK_EQ(source->replicate_now(), ERR_INVALID_PARAMETER);
	CHECK(Array(server->command("entities")).is_empty());
	config->set_interest_radius(0.0);
	actor->set_process_priority(17);
	CHECK_EQ(source->replicate_now(), OK);
	CHECK_EQ(Array(server->command("entities")).size(), 1);
	CHECK_EQ(client->connect_to_server("127.0.0.1", int(server->get_statistics()["local_port"])), OK);
	auto pump = [&](int expected) {
		const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + 2000000;
		bool poll_ok = true;
		while (OS::get_singleton()->get_ticks_usec() < deadline) {
			poll_ok = server->poll() == OK && client->poll() == OK && poll_ok;
			if (client->get_state() == "Connected") {
				poll_ok = replica->replicate_now() == OK && poll_ok;
				if (copy->get_process_priority() == expected) {
					break;
				}
			}
			OS::get_singleton()->delay_usec(2000);
		}
		CHECK(poll_ok);
		CHECK_EQ(client->get_state(), "Connected");
		CHECK_EQ(copy->get_process_priority(), expected);
	};
	pump(17);
	Array before = server->command("entities");
	const uint64_t sent = uint64_t(source->get_statistics()["sent"]);
	config->set_interest_radius(5.0);
	actor->set_process_priority(33);
	CHECK_EQ(source->replicate_now(), ERR_UNAVAILABLE);
	CHECK_EQ(Array(server->command("entities")), before);
	CHECK_EQ(uint64_t(source->get_statistics()["sent"]), sent);
	config->set_interest_radius(0.0);
	actor->set_process_priority(47);
	CHECK_EQ(source->replicate_now(), OK);
	pump(47);
	server->stop();
	client->stop();
	memdelete(actor);
	memdelete(copy);
}
#endif
} // namespace TestSuperposition
