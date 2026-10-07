/**************************************************************************/
/*  superposition_usage.hpp                                               */
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
#include <godot_cpp/classes/egp_net_session.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/superposition.hpp>
#include <godot_cpp/classes/superposition_config.hpp>
#include <godot_cpp/classes/superposition_property.hpp>
#include <godot_cpp/core/memory.hpp>
namespace egp::examples {
// Optional native C++ construction. The saved Inspector scene needs none of this.
inline godot::Superposition *attach(godot::Node3D *target, const godot::Ref<godot::EGPNetSession> &session, const godot::String &stable_key) {
	godot::Ref<godot::SuperpositionProperty> visible;
	visible.instantiate();
	visible->set_property("visible");
	visible->set_value_type(godot::Variant::BOOL);
	visible->set_quantization(0);
	godot::TypedArray<godot::SuperpositionProperty> rules;
	rules.push_back(visible);
	godot::Ref<godot::SuperpositionConfig> config;
	config.instantiate();
	config->set_properties(rules);
	config->set_update_rate(10);
	config->set_priority(8);
	config->set_capture_mode(1);
	auto *replication = memnew(godot::Superposition);
	replication->set_config(config);
	replication->set_replication_key(stable_key);
	target->add_child(replication);
	replication->set_session(session);
	return replication;
}
inline void set_visible(godot::Node3D *target, godot::Superposition *replication, bool visible) {
	target->set_visible(visible);
	replication->mark_dirty();
}
} //namespace egp::examples
