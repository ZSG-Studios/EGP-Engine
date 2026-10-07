/**************************************************************************/
/*  superposition.cpp                                                     */
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

#include "superposition.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/object/class_db.h"
#include "scene/3d/node_3d.h"

namespace {
bool supported(int t) {
	return t == Variant::BOOL || t == Variant::INT || t == Variant::FLOAT || t == Variant::VECTOR3 || t == Variant::STRING;
}
} //namespace
void SuperpositionProperty::_bind_methods() {
#define SP_BIND(n, s, g, t, h, v) \
	ClassDB::bind_method(D_METHOD(#s, "value"), &SuperpositionProperty::s); \
	ClassDB::bind_method(D_METHOD(#g), &SuperpositionProperty::g); \
	ADD_PROPERTY(PropertyInfo(t, n, h, v), #s, #g)
	SP_BIND("enabled", set_enabled, is_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
	SP_BIND("property", set_property, get_property, Variant::STRING_NAME, PROPERTY_HINT_NONE, "");
	SP_BIND("value_type", set_value_type, get_value_type, Variant::INT, PROPERTY_HINT_ENUM, "Boolean:1,Integer:2,Float:3,String:4,Vector3:9");
	SP_BIND("quantization", set_quantization, get_quantization, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,100,0.001,or_greater");
	SP_BIND("smoothing", set_smoothing, is_smoothing, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef SP_BIND
}
void SuperpositionConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_properties", "properties"), &SuperpositionConfig::set_properties);
	ClassDB::bind_method(D_METHOD("get_properties"), &SuperpositionConfig::get_properties);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "properties", PROPERTY_HINT_ARRAY_TYPE, "SuperpositionProperty"), "set_properties", "get_properties");
	ClassDB::bind_method(D_METHOD("set_update_rate", "value"), &SuperpositionConfig::set_update_rate);
	ClassDB::bind_method(D_METHOD("get_update_rate"), &SuperpositionConfig::get_update_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "update_rate", PROPERTY_HINT_RANGE, "1,30,1"), "set_update_rate", "get_update_rate");
	ClassDB::bind_method(D_METHOD("set_interest_radius", "value"), &SuperpositionConfig::set_interest_radius);
	ClassDB::bind_method(D_METHOD("get_interest_radius"), &SuperpositionConfig::get_interest_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interest_radius", PROPERTY_HINT_RANGE, "0,1000,1,or_greater"), "set_interest_radius", "get_interest_radius");
	ClassDB::bind_method(D_METHOD("set_interest_hysteresis", "value"), &SuperpositionConfig::set_interest_hysteresis);
	ClassDB::bind_method(D_METHOD("get_interest_hysteresis"), &SuperpositionConfig::get_interest_hysteresis);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interest_hysteresis", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_interest_hysteresis", "get_interest_hysteresis");
	ClassDB::bind_method(D_METHOD("set_priority", "value"), &SuperpositionConfig::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &SuperpositionConfig::get_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "1,16,1"), "set_priority", "get_priority");
	ClassDB::bind_method(D_METHOD("set_capture_mode", "value"), &SuperpositionConfig::set_capture_mode);
	ClassDB::bind_method(D_METHOD("get_capture_mode"), &SuperpositionConfig::get_capture_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "capture_mode", PROPERTY_HINT_ENUM, "Automatic,Pushed"), "set_capture_mode", "get_capture_mode");
}
Node *Superposition::target() const {
	return is_inside_tree() ? get_node_or_null(target_path) : nullptr;
}
String Superposition::effective_key() const {
	return replication_key.is_empty() && target() ? String(target()->get_path()) : replication_key;
}
void Superposition::reset_binding() {
	Ref<EGPNetSession> active = session;
	set_session(Ref<EGPNetSession>());
	set_session(active);
}
void Superposition::set_session(const Ref<EGPNetSession> &v) {
	if (session == v) {
		return;
	}
	if (owns_entity && session.is_valid() && session->get_state() == "Listening") {
		Dictionary args;
		args["entity"] = entity;
		Error e = Error(int(session->command("despawn", args)));
		if (e != OK && e != ERR_DOES_NOT_EXIST) {
			fail(e, "Could not retire previous session's entity.");
		}
	}
	session = v;
	entity = 0;
	owns_entity = false;
	last_revision = -1;
	last_payload.clear();
	desired.clear();
	dirty = true;
	applied_priority = 0;
	visibility.clear();
}
Error Superposition::fail(Error p_error, const String &p_text) {
	last_error = p_text;
	rejected++;
	emit_signal("replication_error", p_error, p_text);
	return p_error;
}
Error Superposition::schema(Array &r_rules) const {
	if (config.is_null() || !target()) {
		return ERR_UNCONFIGURED;
	}
	auto rules = config->get_properties();
	if (rules.size() > 32 || !Math::is_finite(config->get_update_rate()) || config->get_capture_mode() < 0 || config->get_capture_mode() > 1 || config->get_priority() < 1 || config->get_priority() > 16 || !Math::is_finite(config->get_interest_hysteresis()) || config->get_interest_hysteresis() < 0 || config->get_interest_hysteresis() > 1e6 || config->get_update_rate() < 1 || config->get_update_rate() > 30 || !Math::is_finite(config->get_interest_radius()) || config->get_interest_radius() < 0 || effective_key().is_empty() || effective_key().length() > 256 || entity_kind < 0) {
		return ERR_INVALID_PARAMETER;
	}
	HashSet<StringName> names;
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		if (rule.is_null()) {
			return ERR_INVALID_PARAMETER;
		}
		if (!rule->is_enabled()) {
			continue;
		}
		if (!supported(rule->get_value_type()) || String(rule->get_property()).is_empty() || names.has(rule->get_property()) || !Math::is_finite(rule->get_quantization()) || rule->get_quantization() < 0 || (rule->get_quantization() != 0 && (rule->get_quantization() < 1e-9 || rule->get_quantization() > 1e6))) {
			return ERR_INVALID_PARAMETER;
		}
		bool valid = false;
		Variant current = target()->get(rule->get_property(), &valid);
		if (!valid || current.get_type() != rule->get_value_type()) {
			return ERR_INVALID_PARAMETER;
		}
		names.insert(rule->get_property());
		r_rules.push_back(rule);
	}
	return r_rules.is_empty() ? ERR_UNCONFIGURED : OK;
}
Error Superposition::normalize(const Ref<SuperpositionProperty> &rule, const Variant &value, Variant &out) const {
	if (value.get_type() != rule->get_value_type()) {
		return ERR_INVALID_DATA;
	}
	out = value;
	double q = rule->get_quantization();
	if (value.get_type() == Variant::FLOAT) {
		double v = value;
		if (!Math::is_finite(v) || Math::abs(v) > 1e12) {
			return ERR_INVALID_DATA;
		}
		out = q > 0 ? Math::snapped(v, q) : v;
	} else if (value.get_type() == Variant::VECTOR3) {
		Vector3 v = value;
		if (!v.is_finite() || Math::abs(v.x) > 1e12 || Math::abs(v.y) > 1e12 || Math::abs(v.z) > 1e12) {
			return ERR_INVALID_DATA;
		}
		out = q > 0 ? v.snapped(Vector3(q, q, q)) : v;
	} else if (value.get_type() == Variant::STRING && String(value).utf8().length() > 256) {
		return ERR_INVALID_DATA;
	}
	return OK;
}
PackedByteArray Superposition::capture_state() {
	Array rules;
	Error e = schema(rules);
	if (e != OK) {
		fail(e, "Invalid target or Superposition schema (1-32 typed properties required).");
		return PackedByteArray();
	}
	Array rows;
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		Variant value;
		if (normalize(rule, target()->get(rule->get_property()), value) != OK) {
			fail(ERR_INVALID_DATA, "Non-finite, oversized or incorrectly typed replicated property.");
			return PackedByteArray();
		}
		Array row;
		row.push_back(String(rule->get_property()));
		row.push_back(rule->get_value_type());
		row.push_back(rule->get_quantization());
		row.push_back(value);
		rows.push_back(row);
	}
	Array envelope;
	envelope.push_back(1);
	envelope.push_back(effective_key());
	envelope.push_back(rows);
	int size = 0;
	if (encode_variant(envelope, nullptr, size, false) != OK || size > 4096) {
		fail(ERR_OUT_OF_MEMORY, "Superposition state exceeds 4096 bytes.");
		return PackedByteArray();
	}
	PackedByteArray bytes;
	bytes.resize(size);
	if (encode_variant(envelope, bytes.ptrw(), size, false) != OK) {
		return PackedByteArray();
	}
	captures++;
	return bytes;
}
Error Superposition::apply_state(const PackedByteArray &bytes) {
	// Application is a server-to-client operation. Never let a client command alter server state.
	if (session.is_valid() && session->get_state() == "Listening") {
		return fail(ERR_UNAUTHORIZED, "Server rejects incoming Superposition property state.");
	}
	if (bytes.is_empty() || bytes.size() > 4096) {
		return fail(ERR_INVALID_DATA, "Invalid Superposition state length.");
	}
	Variant decoded;
	int consumed = 0;
	if (decode_variant(decoded, bytes.ptr(), bytes.size(), &consumed, false) != OK || consumed != bytes.size() || decoded.get_type() != Variant::ARRAY) {
		return fail(ERR_INVALID_DATA, "Malformed Superposition envelope.");
	}
	Array envelope = decoded, rules;
	if (schema(rules) != OK || envelope.size() != 3 || envelope[0].get_type() != Variant::INT || int64_t(envelope[0]) != 1 || envelope[1].get_type() != Variant::STRING || String(envelope[1]) != effective_key() || envelope[2].get_type() != Variant::ARRAY) {
		return fail(ERR_INVALID_DATA, "Superposition protocol/key/schema mismatch.");
	}
	Array rows = envelope[2], normalized;
	if (rows.size() != rules.size()) {
		return fail(ERR_INVALID_DATA, "Superposition property count mismatch.");
	}
	for (int i = 0; i < rules.size(); i++) {
		if (rows[i].get_type() != Variant::ARRAY) {
			return fail(ERR_INVALID_DATA, "Malformed property row.");
		}
		Array row = rows[i];
		Ref<SuperpositionProperty> rule = rules[i];
		Variant value;
		if (row.size() != 4 || row[0].get_type() != Variant::STRING || String(row[0]) != String(rule->get_property()) || row[1].get_type() != Variant::INT || int(row[1]) != rule->get_value_type() || row[2].get_type() != Variant::FLOAT || double(row[2]) != rule->get_quantization() || normalize(rule, row[3], value) != OK || value != row[3]) {
			return fail(ERR_INVALID_DATA, "Superposition type, quantization or property schema mismatch.");
		}
		normalized.push_back(value);
	}
	// Validate every property before mutating any target property.
	desired = normalized;
	desired_names.clear();
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		desired_names.push_back(rule->get_property());
	}
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		if (!rule->is_smoothing() || (rule->get_value_type() != Variant::FLOAT && rule->get_value_type() != Variant::VECTOR3)) {
			target()->set(rule->get_property(), normalized[i]);
		}
	}
	applied++;
	last_error = String();
	emit_signal("state_applied");
	return OK;
}
Error Superposition::set_observer_position(int64_t peer, const Vector3 &position) {
	if (peer < 0 || !position.is_finite() || (!observers.has(peer) && observers.size() >= 64)) {
		return fail(ERR_INVALID_PARAMETER, "Invalid observer or observer budget exceeded.");
	}
	observers[peer] = position;
	return OK;
}
void Superposition::clear_observer(int64_t peer) {
	if (session.is_valid() && session->get_state() == "Listening" && entity != 0 && visibility.has(peer) && !bool(visibility[peer])) {
		Dictionary args;
		args["entity"] = entity;
		args["peer"] = peer;
		args["visible"] = true;
		Error e = Error(int(session->command("set_visible", args)));
		if (e != OK) {
			fail(e, "Could not restore observer visibility.");
		}
	}
	observers.erase(peer);
	visibility.erase(peer);
}
Error Superposition::replicate_now() {
	if (config.is_null() || !target()) {
		return fail(ERR_UNCONFIGURED, "Configure a gameplay target and property schema.");
	}
	if (config->get_capture_mode() < 0 || config->get_capture_mode() > 1 || config->get_priority() < 1 || config->get_priority() > 16 || !Math::is_finite(config->get_interest_radius()) || config->get_interest_radius() < 0 || !Math::is_finite(config->get_interest_hysteresis()) || config->get_interest_hysteresis() < 0 || config->get_interest_hysteresis() > 1e6 || !Math::is_finite(config->get_update_rate()) || config->get_update_rate() < 1 || config->get_update_rate() > 30) {
		return fail(ERR_INVALID_PARAMETER, "Invalid Superposition scheduling or interest policy.");
	}
	if (config.is_valid() && config->get_properties().size() <= 32) {
		Array signature;
		auto rules = config->get_properties();
		for (int i = 0; i < rules.size(); i++) {
			Ref<SuperpositionProperty> rule = rules[i];
			if (rule.is_null()) {
				signature.push_back(Variant());
				continue;
			}
			Array row;
			row.push_back(rule->get_property());
			row.push_back(rule->get_value_type());
			row.push_back(rule->get_quantization());
			row.push_back(rule->is_enabled());
			row.push_back(rule->is_smoothing());
			signature.push_back(row);
		}
		if (signature != tracked_schema) {
			reset_binding();
			tracked_schema = signature;
		}
	}
	if (session.is_null()) {
		return fail(ERR_UNCONFIGURED, "Assign a configured EGPNet session or Session Path.");
	}
	const bool server = session->get_state() == "Listening";
	if (server) {
		if (entity != 0) {
			Dictionary lookup;
			lookup["entity"] = entity;
			Variant existing = session->command("entity", lookup);
			bool identity_matches = false;
			if (existing.get_type() == Variant::DICTIONARY && !Dictionary(existing).is_empty()) {
				Dictionary row = existing;
				PackedByteArray state = row["state"];
				Variant decoded;
				if (int(row["kind"]) == entity_kind && state.size() <= 4096 && decode_variant(decoded, state.ptr(), state.size(), nullptr, false) == OK && decoded.get_type() == Variant::ARRAY) {
					Array envelope = decoded;
					identity_matches = envelope.size() == 3 && envelope[1].get_type() == Variant::STRING && String(envelope[1]) == effective_key();
				}
			}
			if (!identity_matches) {
				entity = 0;
				applied_priority = 0;
				dirty = true;
				owns_entity = false;
				visibility.clear();
				last_payload.clear();
			}
		}
		const bool capture = entity == 0 || dirty || config.is_null() || config->get_capture_mode() == 0;
		PackedByteArray bytes = capture ? capture_state() : last_payload;
		if (!capture) {
			++push_skips;
		}
		if (bytes.is_empty()) {
			return ERR_INVALID_DATA;
		}
		if (entity == 0) {
			Array entities = session->command("entities");
			for (int i = 0; i < entities.size(); i++) {
				Dictionary existing = entities[i];
				if (int(existing["kind"]) != entity_kind) {
					continue;
				}
				PackedByteArray state = existing["state"];
				Variant decoded;
				if (state.size() > 4096 || decode_variant(decoded, state.ptr(), state.size(), nullptr, false) != OK || decoded.get_type() != Variant::ARRAY) {
					continue;
				}
				Array envelope = decoded;
				if (envelope.size() == 3 && envelope[1].get_type() == Variant::STRING && String(envelope[1]) == effective_key()) {
					return fail(ERR_ALREADY_EXISTS, "Duplicate Superposition replication key in this session.");
				}
			}
			Dictionary args;
			args["kind"] = entity_kind;
			args["state"] = bytes;
			Variant result = session->command("spawn", args);
			if (result.get_type() != Variant::DICTIONARY) {
				return fail(FAILED, "Native spawn failed.");
			}
			Dictionary row = result;
			Error e = Error(int(row.get("error", FAILED)));
			if (e != OK) {
				return fail(e, "Native spawn rejected Superposition state.");
			}
			entity = row["entity"];
			owns_entity = true;
			last_payload = bytes;
			sent++;
		} else if (bytes != last_payload) {
			Dictionary args;
			args["entity"] = entity;
			args["state"] = bytes;
			Error e = Error(int(session->command("update_entity", args)));
			if (e != OK) {
				return fail(e, "Native entity update failed.");
			}
			last_payload = bytes;
			sent++;
		} else {
			skipped++;
		}
		dirty = false;
		if (applied_priority != config->get_priority()) {
			Dictionary args;
			args["entity"] = entity;
			args["priority"] = config->get_priority();
			Error e = Error(int(session->command("set_replication_priority", args)));
			if (e != OK) {
				return fail(e, "Native replication priority rejected.");
			}
			applied_priority = config->get_priority();
		}
		Node3D *spatial = Object::cast_to<Node3D>(target());
		if (config->get_interest_radius() == 0 || !spatial) {
			Array keys = visibility.keys();
			for (int i = 0; i < keys.size(); i++) {
				if (bool(visibility[keys[i]])) {
					continue;
				}
				Dictionary args;
				args["entity"] = entity;
				args["peer"] = keys[i];
				args["visible"] = true;
				Error e = Error(int(session->command("set_visible", args)));
				if (e != OK && e != ERR_DOES_NOT_EXIST) {
					return fail(e, "Could not restore disabled interest visibility.");
				}
			}
			visibility.clear();
		} else {
			Array observer_keys = observers.keys();
			for (int observer_index = 0; observer_index < observer_keys.size(); observer_index++) {
				Variant peer = observer_keys[observer_index];
				bool connected = false;
				Array peers = session->command("peers");
				for (int p = 0; p < peers.size(); p++) {
					Dictionary row = peers[p];
					if (row["peer_id"] == peer) {
						connected = true;
						break;
					}
				}
				if (!connected) {
					observers.erase(peer);
					visibility.erase(peer);
					continue;
				}
				Vector3 position = observers[peer];
				Dictionary args;
				const double radius = config->get_interest_radius() + (visibility.has(peer) && bool(visibility[peer]) ? config->get_interest_hysteresis() : 0.0);
				bool visible = spatial->get_global_position().distance_squared_to(position) <= radius * radius;
				if (visibility.has(peer) && bool(visibility[peer]) == visible) {
					continue;
				}
				args["entity"] = entity;
				args["peer"] = peer;
				args["visible"] = visible;
				Error e = Error(int(session->command("set_visible", args)));
				if (e != OK) {
					return fail(e, "Native interest visibility update failed; remove disconnected observers.");
				}
				visibility[peer] = visible;
			}
		}
	} else if (session->get_state() == "Connected") {
		if (entity == 0) {
			Array entities = session->command("entities");
			for (int i = 0; i < entities.size(); i++) {
				Dictionary row = entities[i];
				if (int(row["kind"]) != entity_kind) {
					continue;
				}
				PackedByteArray bytes = row["state"];
				Variant value;
				if (bytes.size() > 4096 || decode_variant(value, bytes.ptr(), bytes.size(), nullptr, false) != OK || value.get_type() != Variant::ARRAY) {
					continue;
				}
				Array envelope = value;
				if (envelope.size() == 3 && envelope[1].get_type() == Variant::STRING && String(envelope[1]) == effective_key()) {
					entity = row["entity"];
					break;
				}
			}
		}
		if (entity != 0) {
			Dictionary args;
			args["entity"] = entity;
			Variant value = session->command("entity", args);
			if (value.get_type() == Variant::DICTIONARY) {
				Dictionary row = value;
				if (row.is_empty()) {
					entity = 0;
					last_revision = -1;
					desired.clear();
				} else if (int64_t(row["revision"]) != last_revision) {
					Error e = apply_state(row["state"]);
					if (e != OK) {
						return e;
					}
					last_revision = row["revision"];
				}
			}
		}
	} else {
		entity = 0;
		last_revision = -1;
		desired.clear();
		return ERR_UNCONFIGURED;
	}
	return OK;
}
void Superposition::smooth(double delta) {
	if (desired.is_empty() || !target() || (session.is_valid() && session->get_state() != "Connected")) {
		return;
	}
	Array rules;
	if (schema(rules) != OK || desired.size() != rules.size()) {
		return;
	}
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		if (desired_names.size() != rules.size() || StringName(desired_names[i]) != rule->get_property() || desired[i].get_type() != rule->get_value_type()) {
			desired.clear();
			desired_names.clear();
			return;
		}
	}
	double weight = 1.0 - Math::exp(-12.0 * MIN(delta, 0.1));
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		if (!rule->is_smoothing()) {
			continue;
		}
		if (rule->get_value_type() == Variant::FLOAT) {
			target()->set(rule->get_property(), Math::lerp(double(target()->get(rule->get_property())), double(desired[i]), weight));
		} else if (rule->get_value_type() == Variant::VECTOR3) {
			target()->set(rule->get_property(), Vector3(target()->get(rule->get_property())).lerp(desired[i], weight));
		}
	}
}
void Superposition::_notification(int what) {
	if (what == NOTIFICATION_READY) {
		set_process(true);
		notify_property_list_changed();
	} else if (what == NOTIFICATION_PROCESS && enabled && !Engine::get_singleton()->is_editor_hint()) {
		if (!session_path.is_empty()) {
			Node *provider = get_node_or_null(session_path);
			if (provider) {
				set_session(provider->get("session"));
			} else {
				set_session(Ref<EGPNetSession>());
			}
		}
		double delta = get_process_delta_time();
		smooth(delta);
		elapsed += delta;
		if (config.is_valid() && elapsed >= 1.0 / MAX(1.0, config->get_update_rate())) {
			elapsed = 0;
			replicate_now();
		}
	} else if (what == NOTIFICATION_EXIT_TREE && owns_entity && session.is_valid() && session->get_state() == "Listening") {
		Dictionary args;
		args["entity"] = entity;
		session->command("despawn", args);
		entity = 0;
		owns_entity = false;
	}
}
bool Superposition::_set(const StringName &name, const Variant &value) {
	String s = name;
	if (!s.begins_with("replicate/")) {
		return false;
	}
	StringName property = s.substr(10);
	if (config.is_null()) {
		config.instantiate();
	}
	auto rules = config->get_properties();
	for (int i = 0; i < rules.size(); i++) {
		Ref<SuperpositionProperty> rule = rules[i];
		if (rule.is_valid() && rule->get_property() == property) {
			if (!bool(value)) {
				rules.remove_at(i);
				config->set_properties(rules);
			} else {
				rule->set_enabled(true);
			}
			notify_property_list_changed();
			update_configuration_warnings();
			return true;
		}
	}
	if (bool(value) && target()) {
		Ref<SuperpositionProperty> rule;
		rule.instantiate();
		rule->set_property(property);
		rule->set_value_type(target()->get(property).get_type());
		rules.push_back(rule);
		config->set_properties(rules);
		notify_property_list_changed();
		update_configuration_warnings();
	}
	return true;
}
bool Superposition::_get(const StringName &name, Variant &value) const {
	String s = name;
	if (s == "status/last_error") {
		value = last_error;
		return true;
	}
	if (s == "status/statistics") {
		value = get_statistics();
		return true;
	}
	if (!s.begins_with("replicate/")) {
		return false;
	}
	value = false;
	if (config.is_valid()) {
		auto rules = config->get_properties();
		for (int i = 0; i < rules.size(); i++) {
			Ref<SuperpositionProperty> rule = rules[i];
			if (rule.is_valid() && String(rule->get_property()) == s.substr(10)) {
				value = rule->is_enabled();
				break;
			}
		}
	}
	return true;
}
void Superposition::_get_property_list(List<PropertyInfo> *list) const {
	list->push_back(PropertyInfo(Variant::STRING, "status/last_error", PROPERTY_HINT_MULTILINE_TEXT, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
	list->push_back(PropertyInfo(Variant::DICTIONARY, "status/statistics", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
	if (!Engine::get_singleton()->is_editor_hint() || !target()) {
		return;
	}
	List<PropertyInfo> properties;
	target()->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (supported(property.type) && (property.usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR)) && !(property.usage & PROPERTY_USAGE_READ_ONLY) && String(property.name).find("/") < 0) {
			list->push_back(PropertyInfo(Variant::BOOL, "replicate/" + String(property.name), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR));
		}
	}
}
PackedStringArray Superposition::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (!target()) {
		warnings.push_back("Choose a Target node. Supported properties appear as Replicate checkboxes.");
	}
	Array rules;
	if (schema(rules) != OK) {
		warnings.push_back("Select 1-32 supported target properties and use matching server/client configuration.");
	}
	if (session_path.is_empty() && session.is_null()) {
		warnings.push_back("Set Session Path to your configured EGPNet node, or call set_session().");
	}
	return warnings;
}
Dictionary Superposition::get_statistics() const {
	Dictionary stats;
	stats["entity"] = entity;
	stats["captures"] = int64_t(captures);
	stats["sent"] = int64_t(sent);
	stats["dirty_skips"] = int64_t(skipped);
	stats["push_skips"] = int64_t(push_skips);
	stats["priority"] = applied_priority;
	stats["applied"] = int64_t(applied);
	stats["rejected"] = int64_t(rejected);
	stats["last_error"] = last_error;
	stats["state_bytes"] = last_payload.size();
	return stats;
}
void Superposition::_bind_methods() {
#define SP_BIND(n, s, g, t, h, v) \
	ClassDB::bind_method(D_METHOD(#s, "value"), &Superposition::s); \
	ClassDB::bind_method(D_METHOD(#g), &Superposition::g); \
	ADD_PROPERTY(PropertyInfo(t, n, h, v), #s, #g)
	SP_BIND("enabled", set_enabled, is_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
	SP_BIND("target_path", set_target_path, get_target_path, Variant::NODE_PATH, PROPERTY_HINT_NONE, "");
	SP_BIND("session_path", set_session_path, get_session_path, Variant::NODE_PATH, PROPERTY_HINT_NONE, "");
	SP_BIND("config", set_config, get_config, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SuperpositionConfig");
	SP_BIND("replication_key", set_replication_key, get_replication_key, Variant::STRING, PROPERTY_HINT_NONE, "");
	SP_BIND("entity_kind", set_entity_kind, get_entity_kind, Variant::INT, PROPERTY_HINT_RANGE, "0,2147483647,1");
#undef SP_BIND
	ClassDB::bind_method(D_METHOD("set_session", "session"), &Superposition::set_session);
	ClassDB::bind_method(D_METHOD("get_session"), &Superposition::get_session);
	ClassDB::bind_method(D_METHOD("mark_dirty"), &Superposition::mark_dirty);
	ClassDB::bind_method(D_METHOD("replicate_now"), &Superposition::replicate_now);
	ClassDB::bind_method(D_METHOD("capture_state"), &Superposition::capture_state);
	ClassDB::bind_method(D_METHOD("apply_state", "bytes"), &Superposition::apply_state);
	ClassDB::bind_method(D_METHOD("set_observer_position", "peer_id", "position"), &Superposition::set_observer_position);
	ClassDB::bind_method(D_METHOD("clear_observer", "peer_id"), &Superposition::clear_observer);
	ClassDB::bind_method(D_METHOD("get_statistics"), &Superposition::get_statistics);
	ADD_SIGNAL(MethodInfo("replication_error", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("state_applied"));
}
