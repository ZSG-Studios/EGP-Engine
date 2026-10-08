/**************************************************************************/
/*  superposition_spawner.h                                               */
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

#include "egp_net_session.h"

#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

class SuperpositionScene : public Resource {
	GDCLASS(SuperpositionScene, Resource);
	int prefab_id = 1;
	Ref<PackedScene> scene;

protected:
	static void _bind_methods();

public:
	void set_prefab_id(int p_id) {
		prefab_id = p_id;
		emit_changed();
	}
	int get_prefab_id() const { return prefab_id; }
	void set_scene(const Ref<PackedScene> &p_scene) {
		scene = p_scene;
		emit_changed();
	}
	Ref<PackedScene> get_scene() const { return scene; }
};

class SuperpositionSpawner : public Node {
	GDCLASS(SuperpositionSpawner, Node);
	TypedArray<SuperpositionScene> scenes;
	NodePath spawn_path = NodePath("..");
	NodePath session_path;
	String replication_key = "world";
	int entity_kind = 32002;
	int max_instances = 256;
	Ref<EGPNetSession> session;
	uint64_t generation = 0;
	ObjectID automatic_provider;
	struct Instance {
		ObjectID node;
		int64_t owner = -1;
		int prefab = 0;
	};
	HashMap<int64_t, Instance> instances;
	Dictionary peer_schemas;
	Dictionary desired_visibility;
	bool server_schema_matched = false;
	String negotiated_fingerprint;
	String last_error;
	uint64_t spawned = 0, removed = 0, rejected = 0;
	Error fail(Error p_error, const String &p_text);
	Error validate_scenes() const;
	Ref<PackedScene> find_scene(int p_id) const;
	Error instantiate_entity(int64_t p_entity, int p_prefab, int64_t p_owner, const Dictionary &p_data);
	void retire(int64_t p_entity);
	void clear_instances(bool p_retire_entities, const Ref<EGPNetSession> &p_retirement_session = Ref<EGPNetSession>());
	void bind_replication(Node *p_node, Node *p_root, int64_t p_entity, const String &p_component);
	bool refresh_provider(Node *p_provider, bool p_automatic);
	void receive_schema(int64_t p_peer, const PackedByteArray &p_payload);
	void update_handshake();
	void peer_disconnected(int64_t p_peer);
	void send_schema(int64_t p_peer);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_scenes(const TypedArray<SuperpositionScene> &p_scenes);
	TypedArray<SuperpositionScene> get_scenes() const { return scenes; }
	void set_spawn_path(const NodePath &p_path);
	NodePath get_spawn_path() const { return spawn_path; }
	void set_session_path(const NodePath &p_path) {
		if (session_path != p_path) {
			session_path = p_path;
			set_session(Ref<EGPNetSession>());
		}
	}
	NodePath get_session_path() const { return session_path; }
	void set_session(const Ref<EGPNetSession> &p_session);
	Ref<EGPNetSession> get_session() const { return session; }
	void set_replication_key(const String &p_key);
	String get_replication_key() const { return replication_key; }
	void set_entity_kind(int p_kind);
	int get_entity_kind() const { return entity_kind; }
	void set_max_instances(int p_max) { max_instances = CLAMP(p_max, 1, 4096); }
	int get_max_instances() const { return max_instances; }
	int64_t spawn(int p_prefab_id, int64_t p_owner_peer = -1, const Dictionary &p_data = Dictionary());
	Error despawn(int64_t p_entity);
	Error set_visible(int64_t p_entity, int64_t p_peer, bool p_visible);
	Error synchronize();
	Node *get_spawned_node(int64_t p_entity) const;
	int64_t get_owner_client_id(int64_t p_entity) const;
	bool has_entity(int64_t p_entity) const;
	int64_t get_entity_for_node(Node *p_node) const;
	String get_scene_fingerprint() const;
	bool is_peer_compatible(int64_t p_peer) const;
	bool resolve_peer_identity(int64_t p_peer, int64_t &r_client_id) const;
	Dictionary get_statistics() const;
	PackedStringArray get_configuration_warnings() const override;
};
