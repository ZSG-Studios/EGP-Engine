/**************************************************************************/
/*  superposition_rpc.h                                                   */
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

#include "superposition_spawner.h"

#include "core/templates/hash_map.h"

class SuperpositionRPCMethod : public Resource {
	GDCLASS(SuperpositionRPCMethod, Resource);
	int method_id = 1;
	StringName method;
	int permission = 0;
	PackedInt32Array argument_types;
	int calls_per_second = 16;

protected:
	static void _bind_methods();

public:
	enum Permission { AUTHORITY,
		OWNER,
		ANY_PEER };
	void set_method_id(int p_id) {
		method_id = p_id;
		emit_changed();
	}
	int get_method_id() const { return method_id; }
	void set_method(const StringName &p_name) {
		method = p_name;
		emit_changed();
	}
	StringName get_method() const { return method; }
	void set_permission(int p_permission) {
		permission = p_permission;
		emit_changed();
	}
	int get_permission() const { return permission; }
	void set_argument_types(const PackedInt32Array &p_types) {
		argument_types = p_types;
		emit_changed();
	}
	PackedInt32Array get_argument_types() const { return argument_types; }
	void set_calls_per_second(int p_limit) {
		calls_per_second = p_limit;
		emit_changed();
	}
	int get_calls_per_second() const { return calls_per_second; }
};
VARIANT_ENUM_CAST(SuperpositionRPCMethod::Permission);

class SuperpositionRPC : public Node {
	GDCLASS(SuperpositionRPC, Node);
	NodePath spawner_path;
	TypedArray<SuperpositionRPCMethod> methods;
	Ref<EGPNetSession> session;
	Array pending;
	uint64_t generation = 0;
	uint64_t sent = 0, received = 0, rejected = 0;
	String last_error;
	struct Rate {
		uint64_t second = 0;
		int count = 0;
	};
	HashMap<String, Rate> rates;
	void peer_disconnected(int64_t p_peer);
	void receive_application(int64_t p_peer, const PackedByteArray &p_payload);
	Error fail(Error p_error, const String &p_message);
	Ref<SuperpositionRPCMethod> find_method(int p_method_id) const;
	Error validate_methods() const;
	bool valid_arguments(const Ref<SuperpositionRPCMethod> &p_rule, const Array &p_arguments) const;
	bool permit(const Ref<SuperpositionRPCMethod> &p_rule, SuperpositionSpawner *p_spawner, int64_t p_entity, bool p_authority, int64_t p_identity) const;
	bool rate_limit(int64_t p_peer, int p_method, int p_limit);
	void bind_session(const Ref<EGPNetSession> &p_session);
	void dispatch_pending();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_spawner_path(const NodePath &p_path);
	NodePath get_spawner_path() const { return spawner_path; }
	void set_methods(const TypedArray<SuperpositionRPCMethod> &p_methods);
	TypedArray<SuperpositionRPCMethod> get_methods() const { return methods; }
	SuperpositionSpawner *get_spawner() const;
	Error send_rpc(int64_t p_entity, int p_method_id, const Array &p_arguments = Array(), int64_t p_peer = -1);
	String get_method_fingerprint() const;
	Dictionary get_statistics() const;
	PackedStringArray get_configuration_warnings() const override;
};
