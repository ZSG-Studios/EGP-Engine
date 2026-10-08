/**************************************************************************/
/*  superposition_spawner.cpp                                             */
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

#include "superposition_spawner.h"

#include "superposition.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"

#include <cstring>

namespace {
bool valid_data(const Dictionary &p_data) {
	if (p_data.size() > 16) {
		return false;
	}
	Array keys = p_data.keys();
	for (int i = 0; i < keys.size(); i++) {
		if (keys[i].get_type() != Variant::STRING || String(keys[i]).utf8().length() > 64) {
			return false;
		}
		Variant v = p_data[keys[i]];
		switch (v.get_type()) {
			case Variant::NIL:
			case Variant::BOOL:
			case Variant::INT:
				break;
			case Variant::FLOAT:
				if (!Math::is_finite(double(v))) {
					return false;
				}
				break;
			case Variant::STRING:
				if (String(v).utf8().length() > 256) {
					return false;
				}
				break;
			case Variant::VECTOR3:
				if (!Vector3(v).is_finite()) {
					return false;
				}
				break;
			default:
				return false;
		}
	}
	return true;
}
String stable_resource_path(const String &p_path) {
	String path = p_path;
	if (path.get_extension() == "gdc") {
		path = path.get_basename() + ".gd";
	}
	return path;
}
String fingerprint_value(const Variant &p_value, int p_depth = 0);
String fingerprint_property(const PropertyInfo &p_property) {
	return String(p_property.name).c_escape() + ":" + itos(p_property.type) + ":" + String(p_property.class_name).c_escape() + ":" + itos(p_property.hint) + ":" + p_property.hint_string.c_escape() + ":" + itos(p_property.usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_SCRIPT_VARIABLE));
}
String fingerprint_script(const Ref<Script> &p_script, int p_depth) {
	String result = "Script:" + stable_resource_path(p_script->get_path()) + ":" + String(p_script->get_global_name()) + "\n";
	List<MethodInfo> methods;
	p_script->get_script_method_list(&methods);
	Vector<String> method_rows;
	for (const MethodInfo &method : methods) {
		String row = String(method.name).c_escape() + ":" + itos(method.flags) + ":" + fingerprint_property(method.return_val);
		for (const PropertyInfo &arg : method.arguments) {
			row += ":arg:" + fingerprint_property(arg);
		}
		for (const Variant &value : method.default_arguments) {
			row += ":default:" + fingerprint_value(value, p_depth + 1);
		}
		method_rows.push_back(row);
	}
	method_rows.sort();
	for (const String &row : method_rows) {
		result += "method:" + row + "\n";
	}
	List<PropertyInfo> properties;
	p_script->get_script_property_list(&properties);
	Vector<String> property_rows;
	for (const PropertyInfo &property : properties) {
		String row = fingerprint_property(property);
		Variant value;
		if (p_script->get_property_default_value(property.name, value)) {
			row += ":default:" + fingerprint_value(value, p_depth + 1);
		}
		property_rows.push_back(row);
	}
	property_rows.sort();
	for (const String &row : property_rows) {
		result += "property:" + row + "\n";
	}
	result += "rpc:" + fingerprint_value(p_script->get_rpc_config(), p_depth + 1);
	return result.sha256_text();
}
String fingerprint_value(const Variant &p_value, int p_depth) {
	String result = itos(p_value.get_type()) + ":";
	if (p_depth > 8) {
		return result + "depth-limit";
	}
	if (p_value.get_type() == Variant::ARRAY) {
		Array values = p_value;
		result += itos(values.size()) + "[";
		for (int i = 0; i < values.size(); i++) {
			result += fingerprint_value(values[i], p_depth + 1).c_escape() + ";";
		}
		return result + "]";
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		Dictionary values = p_value;
		Array keys = values.keys();
		Vector<String> rows;
		for (int i = 0; i < keys.size(); i++) {
			rows.push_back(fingerprint_value(keys[i], p_depth + 1).c_escape() + "=" + fingerprint_value(values[keys[i]], p_depth + 1).c_escape());
		}
		rows.sort();
		for (const String &row : rows) {
			result += row + ";";
		}
		return result;
	}
	if (p_value.get_type() == Variant::OBJECT) {
		Ref<Resource> resource = p_value;
		if (resource.is_null()) {
			return result + "null-or-nonresource";
		}
		result += resource->get_class() + ":" + stable_resource_path(resource->get_path());
		Ref<Script> script = resource;
		if (script.is_valid()) {
			return result + ":" + fingerprint_script(script, p_depth + 1);
		}
		// Include nested replication schemas without hashing render assets or process-specific object IDs.
		if (Object::cast_to<SuperpositionConfig>(resource.ptr()) || Object::cast_to<SuperpositionProperty>(resource.ptr())) {
			List<PropertyInfo> properties;
			resource->get_property_list(&properties);
			Vector<String> rows;
			for (const PropertyInfo &property : properties) {
				if ((property.usage & PROPERTY_USAGE_STORAGE) == 0 || property.name == "resource_path" || property.name == "script") {
					continue;
				}
				rows.push_back(fingerprint_property(property) + "=" + fingerprint_value(resource->get(property.name), p_depth + 1).c_escape());
			}
			rows.sort();
			for (const String &row : rows) {
				result += "\n" + row;
			}
		}
		return result;
	}
	if (p_value.get_type() == Variant::RID || p_value.get_type() == Variant::CALLABLE || p_value.get_type() == Variant::SIGNAL) {
		return result;
	}
	return result + p_value.get_construct_string();
}

} //namespace
void SuperpositionScene::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_prefab_id", "id"), &SuperpositionScene::set_prefab_id);
	ClassDB::bind_method(D_METHOD("get_prefab_id"), &SuperpositionScene::get_prefab_id);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "prefab_id", PROPERTY_HINT_RANGE, "1,65535,1"), "set_prefab_id", "get_prefab_id");
	ClassDB::bind_method(D_METHOD("set_scene", "scene"), &SuperpositionScene::set_scene);
	ClassDB::bind_method(D_METHOD("get_scene"), &SuperpositionScene::get_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");
}
Error SuperpositionSpawner::fail(Error p_error, const String &p_text) {
	last_error = p_text;
	++rejected;
	emit_signal("spawn_error", p_error, p_text);
	return p_error;
}
Error SuperpositionSpawner::validate_scenes() const {
	if (scenes.is_empty() || scenes.size() > 64 || replication_key.is_empty() || replication_key.utf8().length() > 128 || entity_kind < 0) {
		return ERR_INVALID_PARAMETER;
	}
	HashSet<int> ids;
	for (int i = 0; i < scenes.size(); i++) {
		Ref<SuperpositionScene> entry = scenes[i];
		if (entry.is_null() || entry->get_scene().is_null() || entry->get_prefab_id() < 1 || entry->get_prefab_id() > 65535 || ids.has(entry->get_prefab_id())) {
			return ERR_INVALID_PARAMETER;
		}
		ids.insert(entry->get_prefab_id());
	}
	return OK;
}
Ref<PackedScene> SuperpositionSpawner::find_scene(int p_id) const {
	for (int i = 0; i < scenes.size(); i++) {
		Ref<SuperpositionScene> entry = scenes[i];
		if (entry.is_valid() && entry->get_prefab_id() == p_id) {
			return entry->get_scene();
		}
	}
	return Ref<PackedScene>();
}
void SuperpositionSpawner::bind_replication(Node *p_node, Node *p_root, int64_t p_entity, const String &p_component) {
	Superposition *replication = Object::cast_to<Superposition>(p_node);
	if (replication) {
		replication->set_session(session);
		replication->set_replication_key(replication_key + "/" + itos(p_entity) + "/" + p_component);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		bind_replication(child, p_root, p_entity, p_component + "/" + String(child->get_name()));
	}
}
Error SuperpositionSpawner::instantiate_entity(int64_t p_entity, int p_prefab, int64_t p_owner, const Dictionary &p_data) {
	const ObjectID self_id = get_instance_id();
	const uint64_t operation_generation = generation;
	Node *parent = is_inside_tree() ? get_node_or_null(spawn_path) : nullptr;
	Ref<PackedScene> scene = find_scene(p_prefab);
	if (!parent || scene.is_null() || parent->is_queued_for_deletion()) {
		return fail(ERR_UNCONFIGURED, "Spawn parent or allowlisted PackedScene is unavailable.");
	}
	if (instances.size() >= uint32_t(max_instances)) {
		return fail(ERR_OUT_OF_MEMORY, "SuperpositionSpawner instance limit reached.");
	}
	const ObjectID parent_id = parent->get_instance_id();
	Node *node = scene->instantiate();
	if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion() || ObjectDB::get_instance(parent_id) != parent || parent->is_queued_for_deletion()) {
		if (node) {
			memdelete(node);
		}
		return ERR_BUSY;
	}
	if (!node) {
		return fail(ERR_CANT_CREATE, "Allowlisted PackedScene could not be instantiated.");
	}
	node->set_name("Spawn_" + itos(p_entity));
	node->set_meta("superposition_entity_id", p_entity);
	node->set_meta("superposition_owner_client_id", p_owner);
	node->set_meta("superposition_spawn_data", p_data);
	Instance instance;
	instance.node = node->get_instance_id();
	instance.owner = p_owner;
	instance.prefab = p_prefab;
	instances.insert(p_entity, instance);
	bind_replication(node, node, p_entity, ".");
	const ObjectID node_id = node->get_instance_id();
	parent->add_child(node);
	if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
		return ERR_BUSY;
	}
	if (ObjectDB::get_instance(node_id) != node || node->is_queued_for_deletion() || !instances.has(p_entity)) {
		instances.erase(p_entity);
		return ERR_CANT_CREATE;
	}
	++spawned;
	emit_signal("spawned", p_entity, node, p_data);
	if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
		return ERR_BUSY;
	}
	return get_spawned_node(p_entity) == node ? OK : ERR_CANT_CREATE;
}
void SuperpositionSpawner::retire(int64_t p_entity) {
	Instance *instance = instances.getptr(p_entity);
	if (!instance) {
		return;
	}
	ObjectID node_id = instance->node;
	instances.erase(p_entity);
	Array visibility_keys = desired_visibility.keys();
	for (int i = 0; i < visibility_keys.size(); i++) {
		if (String(visibility_keys[i]).begins_with(itos(p_entity) + "/")) {
			desired_visibility.erase(visibility_keys[i]);
		}
	}
	Node *node = Object::cast_to<Node>(ObjectDB::get_instance(node_id));
	if (node && !node->is_queued_for_deletion()) {
		node->queue_free();
	}
	++removed;
	emit_signal("despawned", p_entity);
}
void SuperpositionSpawner::clear_instances(bool p_retire_entities, const Ref<EGPNetSession> &p_retirement_session) {
	const ObjectID self_id = get_instance_id();
	const uint64_t operation_generation = ++generation;
	Ref<EGPNetSession> previous = p_retirement_session.is_valid() ? p_retirement_session : session;
	HashMap<int64_t, Instance> retiring(instances);
	instances.clear();
	desired_visibility.clear();
	// Detach the entire old batch before any callback can populate a new session.
	for (const KeyValue<int64_t, Instance> &entry : retiring) {
		if (p_retire_entities && previous.is_valid() && previous->get_state() == "Listening") {
			Dictionary args;
			args["entity"] = entry.key;
			previous->command("despawn", args);
		}
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(entry.value.node));
		if (node && !node->is_queued_for_deletion()) {
			node->queue_free();
		}
	}
	removed += retiring.size();
	for (const KeyValue<int64_t, Instance> &entry : retiring) {
		emit_signal("despawned", entry.key);
		if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion() || generation != operation_generation) {
			return;
		}
	}
}
void SuperpositionSpawner::set_scenes(const TypedArray<SuperpositionScene> &p_scenes) {
	scenes = p_scenes;
	update_configuration_warnings();
	clear_instances(true);
}
void SuperpositionSpawner::set_spawn_path(const NodePath &p_path) {
	if (spawn_path == p_path) {
		return;
	}
	spawn_path = p_path;
	update_configuration_warnings();
	clear_instances(true);
}
void SuperpositionSpawner::set_replication_key(const String &p_key) {
	if (replication_key == p_key) {
		return;
	}
	replication_key = p_key;
	update_configuration_warnings();
	clear_instances(true);
}
void SuperpositionSpawner::set_entity_kind(int p_kind) {
	if (entity_kind == p_kind) {
		return;
	}
	entity_kind = p_kind;
	clear_instances(true);
}
void SuperpositionSpawner::set_session(const Ref<EGPNetSession> &p_session) {
	automatic_provider = ObjectID();
	if (session == p_session) {
		return;
	}
	Ref<EGPNetSession> previous = session;
	if (previous.is_valid() && previous->is_connected("application_received", callable_mp(this, &SuperpositionSpawner::receive_schema))) {
		previous->disconnect("application_received", callable_mp(this, &SuperpositionSpawner::receive_schema));
	}
	if (previous.is_valid() && previous->is_connected("peer_disconnected", callable_mp(this, &SuperpositionSpawner::peer_disconnected))) {
		previous->disconnect("peer_disconnected", callable_mp(this, &SuperpositionSpawner::peer_disconnected));
	}
	// Publish the new binding before retirement feedback; nested setters win.
	session = p_session;
	peer_schemas.clear();
	server_schema_matched = false;
	negotiated_fingerprint = String();
	if (session.is_valid()) {
		session->connect("application_received", callable_mp(this, &SuperpositionSpawner::receive_schema));
		session->connect("peer_disconnected", callable_mp(this, &SuperpositionSpawner::peer_disconnected));
	}
	clear_instances(true, previous);
}
bool SuperpositionSpawner::resolve_peer_identity(int64_t p_peer, int64_t &r_client_id) const {
	if (session.is_null() || session->get_state() != "Listening") {
		return false;
	}
	Array peers = session->command("peers");
	for (int i = 0; i < peers.size(); i++) {
		Dictionary peer = peers[i];
		if (int64_t(peer["peer_id"]) == p_peer) {
			r_client_id = peer["client_id"];
			return true;
		}
	}
	return false;
}
int64_t SuperpositionSpawner::spawn(int p_prefab_id, int64_t p_owner_peer, const Dictionary &p_data) {
	const ObjectID self_id = get_instance_id();
	const uint64_t operation_generation = generation;
	Ref<EGPNetSession> operation = session;
	if (session.is_null() || session->get_state() != "Listening" || !is_inside_tree()) {
		fail(ERR_UNAUTHORIZED, "Only the listening server may spawn scenes.");
		return 0;
	}
	if (validate_scenes() != OK || find_scene(p_prefab_id).is_null() || !valid_data(p_data)) {
		fail(ERR_INVALID_PARAMETER, "Invalid scene allowlist, prefab ID or bounded scalar spawn data.");
		return 0;
	}
	int64_t identity = -1;
	if (p_owner_peer != -1 && !resolve_peer_identity(p_owner_peer, identity)) {
		fail(ERR_UNAUTHORIZED, "Owner must be a currently authenticated peer.");
		return 0;
	}
	Array envelope;
	envelope.push_back(1);
	envelope.push_back(replication_key);
	envelope.push_back(p_prefab_id);
	envelope.push_back(identity);
	envelope.push_back(p_data);
	int size = 0;
	if (encode_variant(envelope, nullptr, size, false) != OK || size > 4088) {
		fail(ERR_INVALID_DATA, "Spawn data exceeds the 4096-byte wire limit.");
		return 0;
	}
	PackedByteArray bytes;
	bytes.resize(size + 8);
	memcpy(bytes.ptrw(), "EGPSPS01", 8);
	encode_variant(envelope, bytes.ptrw() + 8, size, false);
	Dictionary args;
	args["kind"] = entity_kind;
	args["state"] = bytes; // Native state remains server authoritative, regardless of RPC owner.
	Dictionary result = session->command("spawn", args);
	Error error = Error(int(result.get("error", FAILED)));
	if (error != OK) {
		fail(error, "Native replicated scene spawn failed.");
		return 0;
	}
	int64_t entity = result["entity"];
	Array peers = session->command("peers");
	for (int i = 0; i < peers.size(); i++) {
		Dictionary peer = peers[i];
		if (!is_peer_compatible(peer["peer_id"])) {
			Dictionary visibility;
			visibility["entity"] = entity;
			visibility["peer"] = peer["peer_id"];
			visibility["visible"] = false;
			session->command("set_visible", visibility);
		}
	}
	Error instantiate_error = instantiate_entity(entity, p_prefab_id, identity, p_data);
	if (ObjectDB::get_instance(self_id) != this || instantiate_error != OK || generation != operation_generation || is_queued_for_deletion()) {
		args.clear();
		args["entity"] = entity;
		operation->command("despawn", args);
		return 0;
	}
	return entity;
}
Error SuperpositionSpawner::despawn(int64_t p_entity) {
	if (session.is_null() || session->get_state() != "Listening") {
		return fail(ERR_UNAUTHORIZED, "Only the listening server may despawn scenes.");
	}
	if (!instances.has(p_entity)) {
		return fail(ERR_DOES_NOT_EXIST, "Entity is not owned by this spawner.");
	}
	Dictionary args;
	args["entity"] = p_entity;
	Error error = Error(int(session->command("despawn", args)));
	if (error != OK) {
		return fail(error, "Native despawn failed.");
	}
	retire(p_entity);
	return OK;
}
Error SuperpositionSpawner::set_visible(int64_t p_entity, int64_t p_peer, bool p_visible) {
	if (session.is_null() || session->get_state() != "Listening") {
		return fail(ERR_UNAUTHORIZED, "Only the listening server controls scene interest.");
	}
	if (!instances.has(p_entity)) {
		return ERR_DOES_NOT_EXIST;
	}
	int64_t identity;
	if (!resolve_peer_identity(p_peer, identity)) {
		return ERR_UNAUTHORIZED;
	}
	desired_visibility[itos(p_entity) + "/" + itos(p_peer)] = p_visible;
	Dictionary args;
	args["entity"] = p_entity;
	args["peer"] = p_peer;
	args["visible"] = p_visible && is_peer_compatible(p_peer);
	return Error(int(session->command("set_visible", args)));
}
Error SuperpositionSpawner::synchronize() {
	const ObjectID self_id = get_instance_id();
	const uint64_t operation_generation = generation;
	if (session.is_null()) {
		peer_schemas.clear();
		server_schema_matched = false;
		clear_instances(false);
		return ERR_UNCONFIGURED;
	}
	String state = session->get_state();
	if (state != "Listening" && state != "Connected") {
		peer_schemas.clear();
		server_schema_matched = false;
		clear_instances(false);
		return ERR_UNCONFIGURED;
	}
	if (validate_scenes() != OK) {
		return fail(ERR_INVALID_PARAMETER, "Scene IDs must be unique and share the same allowlist on every peer.");
	}
	update_handshake();
	if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
		return ERR_BUSY;
	}
	if (state == "Connected" && !server_schema_matched) {
		return ERR_BUSY;
	}
	Array entities = session->command("entities");
	HashSet<int64_t> alive;
	for (int i = 0; i < entities.size(); i++) {
		Dictionary row = entities[i];
		if (int(row["kind"]) != entity_kind) {
			continue;
		}
		PackedByteArray bytes = row["state"];
		Variant decoded;
		int consumed = 0;
		if (bytes.size() < 8 || bytes.size() > 4096 || memcmp(bytes.ptr(), "EGPSPS01", 8) != 0 || decode_variant(decoded, bytes.ptr() + 8, bytes.size() - 8, &consumed, false) != OK || consumed != bytes.size() - 8 || decoded.get_type() != Variant::ARRAY) {
			continue;
		}
		Array envelope = decoded;
		if (envelope.size() != 5 || envelope[0].get_type() != Variant::INT || int64_t(envelope[0]) != 1 || envelope[1].get_type() != Variant::STRING || String(envelope[1]) != replication_key) {
			continue;
		}
		if (envelope[2].get_type() != Variant::INT || int64_t(envelope[2]) < 1 || int64_t(envelope[2]) > 65535 || envelope[3].get_type() != Variant::INT || int64_t(envelope[3]) < -1 || envelope[4].get_type() != Variant::DICTIONARY || !valid_data(envelope[4]) || find_scene(int(envelope[2])).is_null()) {
			fail(ERR_INVALID_DATA, "Rejected unallowlisted scene or invalid spawn metadata.");
			if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
				return ERR_BUSY;
			}
			continue;
		}
		int64_t entity = row["entity"];
		alive.insert(entity);
		if (state == "Connected" && !instances.has(entity)) {
			instantiate_entity(entity, int(envelope[2]), int64_t(envelope[3]), envelope[4]);
			if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
				return ERR_BUSY;
			}
		}
	}
	Vector<int64_t> gone;
	for (const KeyValue<int64_t, Instance> &entry : instances) {
		if (!alive.has(entry.key) || !get_spawned_node(entry.key)) {
			gone.push_back(entry.key);
		}
	}
	for (int64_t entity : gone) {
		if (state == "Listening" && alive.has(entity)) {
			Dictionary args;
			args["entity"] = entity;
			session->command("despawn", args);
		}
		retire(entity);
		if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
			return ERR_BUSY;
		}
	}
	return OK;
}
Node *SuperpositionSpawner::get_spawned_node(int64_t p_entity) const {
	const Instance *entry = instances.getptr(p_entity);
	Node *node = entry ? Object::cast_to<Node>(ObjectDB::get_instance(entry->node)) : nullptr;
	return node && !node->is_queued_for_deletion() ? node : nullptr;
}
int64_t SuperpositionSpawner::get_owner_client_id(int64_t p_entity) const {
	const Instance *entry = instances.getptr(p_entity);
	return entry ? entry->owner : -1;
}
bool SuperpositionSpawner::has_entity(int64_t p_entity) const {
	return get_spawned_node(p_entity) != nullptr;
}
int64_t SuperpositionSpawner::get_entity_for_node(Node *p_node) const {
	for (const KeyValue<int64_t, Instance> &entry : instances) {
		if (p_node && entry.value.node == p_node->get_instance_id()) {
			return entry.key;
		}
	}
	return 0;
}
Dictionary SuperpositionSpawner::get_statistics() const {
	Dictionary out;
	out["instances"] = instances.size();
	out["scene_fingerprint"] = get_scene_fingerprint();
	out["server_schema_matched"] = server_schema_matched;
	out["peer_schemas"] = peer_schemas.duplicate();
	out["spawned"] = int64_t(spawned);
	out["despawned"] = int64_t(removed);
	out["rejected"] = int64_t(rejected);
	out["last_error"] = last_error;
	return out;
}
PackedStringArray SuperpositionSpawner::get_configuration_warnings() const {
	PackedStringArray out;
	if (validate_scenes() != OK) {
		out.push_back("Assign 1-64 scenes with unique stable prefab IDs; use the same IDs and scenes on all peers.");
	}
	if (spawn_path.is_empty()) {
		out.push_back("Choose a spawn parent.");
	}
	if (session.is_null() && session_path.is_empty()) {
		out.push_back("Assign a session provider path or call set_session().");
	}
	return out;
}
bool SuperpositionSpawner::refresh_provider(Node *p_provider, bool p_automatic) {
	const ObjectID self_id = get_instance_id();
	const ObjectID provider_id = p_provider ? p_provider->get_instance_id() : ObjectID();
	const uint64_t before = generation;
	Ref<EGPNetSession> candidate = p_provider ? Ref<EGPNetSession>(p_provider->has_method("get_session") ? p_provider->call("get_session") : p_provider->get("session")) : Ref<EGPNetSession>();
	if (ObjectDB::get_instance(self_id) != this || generation != before || is_queued_for_deletion()) {
		return false;
	}
	const uint64_t expected = generation + (session != candidate ? 1 : 0);
	set_session(candidate);
	if (ObjectDB::get_instance(self_id) != this || generation != expected || is_queued_for_deletion()) {
		return false;
	}
	if (p_automatic && ObjectDB::get_instance(provider_id) == p_provider) {
		automatic_provider = provider_id;
	}
	return true;
}
void SuperpositionSpawner::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		set_process(true);
	} else if (p_what == NOTIFICATION_PROCESS && !Engine::get_singleton()->is_editor_hint()) {
		if (!session_path.is_empty()) {
			if (!refresh_provider(get_node_or_null(session_path), false)) {
				return;
			}
		} else {
			Node *provider = Object::cast_to<Node>(ObjectDB::get_instance(automatic_provider));
			if (automatic_provider.is_valid()) {
				if (!provider || !provider->is_ancestor_of(this) || !provider->has_method("get_session")) {
					if (!refresh_provider(nullptr, false)) {
						return;
					}
				} else if (!refresh_provider(provider, true)) {
					return;
				}
			}
			if (session.is_null() && automatic_provider.is_null()) {
				for (Node *ancestor = get_parent(); ancestor; ancestor = ancestor->get_parent()) {
					if (ancestor->has_method("get_session")) {
						if (!refresh_provider(ancestor, true)) {
							return;
						}
						break;
					}
				}
			}
		}
		synchronize();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		set_session(Ref<EGPNetSession>());
	}
}
String SuperpositionSpawner::get_scene_fingerprint() const {
	if (validate_scenes() != OK) {
		return String();
	}
	Vector<int> ids;
	for (int i = 0; i < scenes.size(); i++) {
		Ref<SuperpositionScene> scene = scenes[i];
		ids.push_back(scene->get_prefab_id());
	}
	ids.sort();
	String description = "SuperpositionScene/1\n";
	for (int id : ids) {
		Ref<PackedScene> packed = find_scene(id);
		Ref<SceneState> state = packed->get_state();
		description += itos(id) + ":" + packed->get_path() + "\n";
		for (int n = 0; n < state->get_node_count(); n++) {
			description += String(state->get_node_path(n)) + ":" + String(state->get_node_path(n, true)) + ":" + String(state->get_node_name(n)) + ":" + String(state->get_node_type(n)) + "\n";
			Vector<String> property_rows;
			for (int p = 0; p < state->get_node_property_count(n); p++) {
				property_rows.push_back(String(state->get_node_property_name(n, p)).c_escape() + ":" + fingerprint_value(state->get_node_property_value(n, p)).c_escape());
			}
			property_rows.sort();
			for (const String &row : property_rows) {
				description += row + "\n";
			}
		}
	}
	return description.sha256_text();
}
bool SuperpositionSpawner::is_peer_compatible(int64_t p_peer) const {
	return session.is_valid() && (session->get_state() == "Listening" ? bool(peer_schemas.get(p_peer, false)) : p_peer == 0 && server_schema_matched);
}
void SuperpositionSpawner::send_schema(int64_t p_peer) {
	Array envelope;
	envelope.push_back("SPSCHEMA1");
	envelope.push_back(replication_key);
	envelope.push_back(get_scene_fingerprint());
	int size = 0;
	encode_variant(envelope, nullptr, size, false);
	PackedByteArray bytes;
	bytes.resize(size + 8);
	memcpy(bytes.ptrw(), "EGPSPS01", 8);
	encode_variant(envelope, bytes.ptrw() + 8, size, false);
	Error error = session->send_application(p_peer, bytes);
	if (error != OK) {
		fail(error, "Could not send scene compatibility handshake.");
	}
}
void SuperpositionSpawner::receive_schema(int64_t p_peer, const PackedByteArray &p_payload) {
	const ObjectID self_id = get_instance_id();
	const uint64_t operation_generation = generation;
	if (session.is_null() || p_payload.size() < 8 || memcmp(p_payload.ptr(), "EGPSPS01", 8) != 0 || p_payload.size() > 512) {
		return;
	}
	Variant decoded;
	int consumed = 0;
	if (decode_variant(decoded, p_payload.ptr() + 8, p_payload.size() - 8, &consumed, false) != OK || consumed != p_payload.size() - 8 || decoded.get_type() != Variant::ARRAY) {
		return;
	}
	Array envelope = decoded;
	if (envelope.size() != 3 || envelope[0].get_type() != Variant::STRING || String(envelope[0]) != "SPSCHEMA1" || envelope[1].get_type() != Variant::STRING || String(envelope[1]) != replication_key) {
		return;
	}
	bool server = session->get_state() == "Listening";
	int64_t identity;
	if ((server && !resolve_peer_identity(p_peer, identity)) || (!server && (session->get_state() != "Connected" || p_peer != 0))) {
		return;
	}
	bool matches = envelope[2].get_type() == Variant::STRING && String(envelope[2]) == get_scene_fingerprint();
	if (server) {
		peer_schemas[p_peer] = matches;
		send_schema(p_peer);
		if (ObjectDB::get_instance(self_id) != this || generation != operation_generation || is_queued_for_deletion()) {
			return;
		}
		for (const KeyValue<int64_t, Instance> &entry : instances) {
			Dictionary args;
			args["entity"] = entry.key;
			args["peer"] = p_peer;
			args["visible"] = matches && bool(desired_visibility.get(itos(entry.key) + "/" + itos(p_peer), true));
			session->command("set_visible", args);
		}
	} else {
		server_schema_matched = matches;
	}
	if (!matches) {
		if (!server) {
			clear_instances(false);
			if (ObjectDB::get_instance(self_id) != this || generation != operation_generation + 1 || is_queued_for_deletion()) {
				return;
			}
		}
		fail(ERR_INVALID_DATA, "Peer scene allowlist differs: match stable prefab IDs, scene structure and script interfaces.");
		if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion()) {
			return;
		}
	}
	emit_signal("peer_schema_checked", p_peer, matches);
}
void SuperpositionSpawner::peer_disconnected(int64_t p_peer) {
	peer_schemas.erase(p_peer);
	Array visibility_keys = desired_visibility.keys();
	for (int i = 0; i < visibility_keys.size(); i++) {
		if (String(visibility_keys[i]).ends_with("/" + itos(p_peer))) {
			desired_visibility.erase(visibility_keys[i]);
		}
	}
	if (session.is_valid() && session->get_state() != "Listening") {
		server_schema_matched = false;
		peer_schemas.clear();
		clear_instances(false);
	}
}
void SuperpositionSpawner::update_handshake() {
	const ObjectID self_id = get_instance_id();
	String fingerprint = get_scene_fingerprint();
	if (negotiated_fingerprint != fingerprint) {
		bool retire_existing = !negotiated_fingerprint.is_empty();
		negotiated_fingerprint = fingerprint;
		peer_schemas.clear();
		server_schema_matched = false;
		if (retire_existing) {
			const uint64_t expected_generation = generation + 1;
			clear_instances(true);
			if (ObjectDB::get_instance(self_id) != this || generation != expected_generation || is_queued_for_deletion()) {
				return;
			}
		}
	}
	if (session->get_state() == "Connected") {
		if (!peer_schemas.has(0)) {
			peer_schemas[0] = false;
			send_schema(0);
		}
		return;
	}
	Array peers = session->command("peers");
	HashSet<int64_t> alive;
	for (int i = 0; i < peers.size(); i++) {
		Dictionary peer = peers[i];
		int64_t id = peer["peer_id"];
		alive.insert(id);
		if (!peer_schemas.has(id)) {
			peer_schemas[id] = false;
			for (const KeyValue<int64_t, Instance> &entry : instances) {
				Dictionary args;
				args["entity"] = entry.key;
				args["peer"] = id;
				args["visible"] = false;
				session->command("set_visible", args);
			}
			const uint64_t before = generation;
			send_schema(id);
			if (ObjectDB::get_instance(self_id) != this || generation != before || is_queued_for_deletion()) {
				return;
			}
		}
	}
	Array known = peer_schemas.keys();
	for (int i = 0; i < known.size(); i++) {
		if (!alive.has(int64_t(known[i]))) {
			peer_schemas.erase(known[i]);
		}
	}
}
void SuperpositionSpawner::_bind_methods() {
#define SP_PROP(name, setter, getter, type, hint, text) \
	ClassDB::bind_method(D_METHOD(#setter, "value"), &SuperpositionSpawner::setter); \
	ClassDB::bind_method(D_METHOD(#getter), &SuperpositionSpawner::getter); \
	ADD_PROPERTY(PropertyInfo(type, name, hint, text), #setter, #getter)
	SP_PROP("scenes", set_scenes, get_scenes, Variant::ARRAY, PROPERTY_HINT_ARRAY_TYPE, "SuperpositionScene");
	SP_PROP("spawn_path", set_spawn_path, get_spawn_path, Variant::NODE_PATH, PROPERTY_HINT_NONE, "");
	SP_PROP("session_path", set_session_path, get_session_path, Variant::NODE_PATH, PROPERTY_HINT_NONE, "");
	SP_PROP("replication_key", set_replication_key, get_replication_key, Variant::STRING, PROPERTY_HINT_NONE, "");
	SP_PROP("entity_kind", set_entity_kind, get_entity_kind, Variant::INT, PROPERTY_HINT_RANGE, "0,65535,1");
	SP_PROP("max_instances", set_max_instances, get_max_instances, Variant::INT, PROPERTY_HINT_RANGE, "1,4096,1");
#undef SP_PROP
	ClassDB::bind_method(D_METHOD("set_session", "session"), &SuperpositionSpawner::set_session);
	ClassDB::bind_method(D_METHOD("get_session"), &SuperpositionSpawner::get_session);
	ClassDB::bind_method(D_METHOD("spawn", "prefab_id", "owner_peer", "data"), &SuperpositionSpawner::spawn, DEFVAL(-1), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("despawn", "entity"), &SuperpositionSpawner::despawn);
	ClassDB::bind_method(D_METHOD("set_visible", "entity", "peer", "visible"), &SuperpositionSpawner::set_visible);
	ClassDB::bind_method(D_METHOD("synchronize"), &SuperpositionSpawner::synchronize);
	ClassDB::bind_method(D_METHOD("get_spawned_node", "entity"), &SuperpositionSpawner::get_spawned_node);
	ClassDB::bind_method(D_METHOD("get_owner_client_id", "entity"), &SuperpositionSpawner::get_owner_client_id);
	ClassDB::bind_method(D_METHOD("has_entity", "entity"), &SuperpositionSpawner::has_entity);
	ClassDB::bind_method(D_METHOD("get_entity_for_node", "node"), &SuperpositionSpawner::get_entity_for_node);
	ClassDB::bind_method(D_METHOD("get_scene_fingerprint"), &SuperpositionSpawner::get_scene_fingerprint);
	ClassDB::bind_method(D_METHOD("is_peer_compatible", "peer"), &SuperpositionSpawner::is_peer_compatible);
	ClassDB::bind_method(D_METHOD("get_statistics"), &SuperpositionSpawner::get_statistics);
	ADD_SIGNAL(MethodInfo("spawned", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_RESOURCE_TYPE, "Node"), PropertyInfo(Variant::DICTIONARY, "data")));
	ADD_SIGNAL(MethodInfo("despawned", PropertyInfo(Variant::INT, "entity")));
	ADD_SIGNAL(MethodInfo("peer_schema_checked", PropertyInfo(Variant::INT, "peer"), PropertyInfo(Variant::BOOL, "compatible")));
	ADD_SIGNAL(MethodInfo("spawn_error", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
}
