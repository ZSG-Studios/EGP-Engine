/**************************************************************************/
/*  superposition.h                                                       */
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
#include "core/templates/hash_set.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"

class SuperpositionProperty : public Resource {
	GDCLASS(SuperpositionProperty, Resource);
	bool enabled = true;
	StringName property;
	int value_type = Variant::FLOAT;
	double quantization = 0.01;
	bool smoothing = false;

protected:
	static void _bind_methods();

public:
	void set_enabled(bool v) {
		enabled = v;
		emit_changed();
	}
	bool is_enabled() const { return enabled; }
	void set_property(const StringName &v) {
		property = v;
		emit_changed();
	}
	StringName get_property() const { return property; }
	void set_value_type(int v) {
		value_type = v;
		emit_changed();
	}
	int get_value_type() const { return value_type; }
	void set_quantization(double v) {
		quantization = v;
		emit_changed();
	}
	double get_quantization() const { return quantization; }
	void set_smoothing(bool v) {
		smoothing = v;
		emit_changed();
	}
	bool is_smoothing() const { return smoothing; }
};
class SuperpositionConfig : public Resource {
	GDCLASS(SuperpositionConfig, Resource);
	TypedArray<SuperpositionProperty> properties;
	double update_rate = 10.0;
	double interest_radius = 0.0;
	double interest_hysteresis = 2.0;
	int priority = 1;
	int capture_mode = 0;

protected:
	static void _bind_methods();

public:
	void set_properties(const TypedArray<SuperpositionProperty> &v) {
		properties = v;
		emit_changed();
	}
	TypedArray<SuperpositionProperty> get_properties() const { return properties; }
	void set_update_rate(double v) {
		update_rate = v;
		emit_changed();
	}
	double get_update_rate() const { return update_rate; }
	void set_interest_radius(double v) {
		interest_radius = v;
		emit_changed();
	}
	void set_interest_hysteresis(double v) {
		interest_hysteresis = v;
		emit_changed();
	}
	double get_interest_hysteresis() const { return interest_hysteresis; }
	void set_priority(int v) {
		priority = v;
		emit_changed();
	}
	int get_priority() const { return priority; }
	void set_capture_mode(int v) {
		capture_mode = v;
		emit_changed();
	}
	int get_capture_mode() const { return capture_mode; }
	double get_interest_radius() const { return interest_radius; }
};
class Superposition : public Node {
	GDCLASS(Superposition, Node);
	bool enabled = true;
	NodePath target_path = NodePath("..");
	NodePath session_path;
	Ref<SuperpositionConfig> config;
	Ref<EGPNetSession> session;
	String replication_key;
	int entity_kind = 32001;
	int64_t entity = 0;
	int64_t last_revision = -1;
	bool owns_entity = false;
	bool dirty = true;
	int applied_priority = 0;
	uint64_t push_skips = 0;
	double elapsed = 0.0;
	PackedByteArray last_payload;
	Array desired;
	Array desired_names;
	Array tracked_schema;
	Dictionary observers;
	Dictionary visibility;
	uint64_t captures = 0, sent = 0, skipped = 0, applied = 0, rejected = 0;
	String last_error;
	Node *target() const;
	String effective_key() const;
	void reset_binding();
	Error fail(Error p_error, const String &p_text);
	Error normalize(const Ref<SuperpositionProperty> &p_rule, const Variant &p_value, Variant &r_value) const;
	Error schema(Array &r_rules) const;
	void smooth(double p_delta);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_value) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_enabled(bool v) { enabled = v; }
	bool is_enabled() const { return enabled; }
	void set_target_path(const NodePath &v) {
		if (target_path != v) {
			reset_binding();
		}
		target_path = v;
		notify_property_list_changed();
		update_configuration_warnings();
	}
	NodePath get_target_path() const { return target_path; }
	void set_session_path(const NodePath &v) {
		if (session_path != v) {
			set_session(Ref<EGPNetSession>());
		}
		session_path = v;
		update_configuration_warnings();
	}
	NodePath get_session_path() const { return session_path; }
	void set_config(const Ref<SuperpositionConfig> &v) {
		if (config != v) {
			reset_binding();
		}
		config = v;
		notify_property_list_changed();
		update_configuration_warnings();
	}
	Ref<SuperpositionConfig> get_config() const { return config; }
	void set_replication_key(const String &v) {
		if (replication_key != v) {
			reset_binding();
		}
		replication_key = v;
	}
	String get_replication_key() const { return replication_key; }
	void set_entity_kind(int v) {
		if (entity_kind != v) {
			reset_binding();
		}
		entity_kind = v;
	}
	int get_entity_kind() const { return entity_kind; }
	void set_session(const Ref<EGPNetSession> &v);
	Ref<EGPNetSession> get_session() const { return session; }
	void mark_dirty() { dirty = true; }
	Error replicate_now();
	PackedByteArray capture_state();
	Error apply_state(const PackedByteArray &p_bytes);
	Error set_observer_position(int64_t p_peer, const Vector3 &p_position);
	void clear_observer(int64_t p_peer);
	Dictionary get_statistics() const;
	PackedStringArray get_configuration_warnings() const override;
};
