/**************************************************************************/
/*  box2d_collision_object_2d.h                                           */
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

// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.

#include "../box2d_globals.h"
#include "../box2d_project_settings.h"
#include "../shapes/box2d_shape_2d.h"
#include "../shapes/box2d_shape_instance.h"
#include "../spaces/box2d_query.h"

#include "modules/box2d/precompiled.h"

class Box2DSpace2D;
class Box2DArea2D;
class Box2DBody2D;

class Box2DCollisionObject2D {
	friend class Box2DLocalReplay;
public:
	enum Type {
		RIGIDBODY,
		AREA,
	};

	explicit Box2DCollisionObject2D(Type p_type);
	virtual ~Box2DCollisionObject2D() = default;

	Type get_type() const { return type; }

	bool is_area() const { return type == Type::AREA; }
	bool is_body() const { return type == Type::RIGIDBODY; }

	Box2DArea2D *as_area() { return is_area() ? reinterpret_cast<Box2DArea2D *>(this) : nullptr; }
	Box2DBody2D *as_body() { return is_body() ? reinterpret_cast<Box2DBody2D *>(this) : nullptr; }

	const Box2DArea2D *as_area() const { return is_area() ? reinterpret_cast<const Box2DArea2D *>(this) : nullptr; }
	const Box2DBody2D *as_body() const { return is_body() ? reinterpret_cast<const Box2DBody2D *>(this) : nullptr; }

	void free();
	bool is_freed() const { return _is_freed; }

	RID get_rid() const { return rid; }
	bool is_pickable() const { return pickable; }
	void set_pickable(bool p_pickable) { pickable = p_pickable; }
	void set_rid(RID p_rid) { rid = p_rid; }

	void set_space(Box2DSpace2D *p_space);
	Box2DSpace2D *get_space() const { return space; }
	_FORCE_INLINE_ bool in_space() const { return space; }

	void set_mode(PS2DE::BodyMode p_mode);
	PS2DE::BodyMode get_mode() const { return mode; }
	bool is_dynamic() const { return mode > PS2DE::BODY_MODE_KINEMATIC; }
	bool is_static() const { return mode == PS2DE::BODY_MODE_STATIC; }

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() { return shape_def.filter.categoryBits; }
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() { return shape_def.filter.maskBits; }

	void set_transform(const Transform2D &p_transform, bool p_move_kinematic = false);
	Transform2D get_transform() const { return current_transform; }

	b2BodyId get_body_id() const { return body_id; }
	b2ShapeDef get_shape_def() const { return shape_def; }

	void add_shape(Box2DShape2D *p_shape, const Transform2D &p_transform, bool p_disabled);
	void set_shape(int p_index, Box2DShape2D *p_shape);
	void shape_updated(Box2DShape2D *p_shape);
	void remove_shape(int p_index);
	void remove_shape(Box2DShape2D *p_shape);
	void clear_shapes();
	void reindex_all_shapes();

	int32_t get_shape_count() const { return shapes.size(); }
	void set_shape_transform(int p_index, const Transform2D &p_transform);
	Transform2D get_shape_transform(int p_index) const;
	RID get_shape_rid(int p_index) const;
	void set_shape_disabled(int p_index, bool p_disabled);

	void set_instance_id(const ObjectID &p_instance_id) { instance_id = p_instance_id; }
	ObjectID get_instance_id() const { return instance_id; }

	void set_canvas_instance_id(const ObjectID &p_canvas_instance_id) { canvas_instance_id = p_canvas_instance_id; }
	ObjectID get_canvas_instance_id() const { return canvas_instance_id; }

	void set_user_data(const Variant &p_data) { user_data = p_data; }
	Variant get_user_data() const { return user_data; }

	virtual void shapes_changed() {}

	struct CharacterCollideContext {
		b2ShapeId shape_id;
		b2Transform transform;
		Box2DShapePrimitive shape;
		LocalVector<CharacterCollideResult> &results;
	};

	struct CharacterCastContext {
		b2ShapeId shape_id;
		b2Transform transform;
		Box2DShapePrimitive shape;
		CharacterCastResult &result;
		Vector2 motion;
		real_t margin;
	};

	int character_collide(const Transform2D &p_from, real_t p_margin, LocalVector<CharacterCollideResult> &p_results);
	CharacterCastResult character_cast(const Transform2D &p_from, real_t p_margin, Vector2 p_motion);

protected:
	void build_shape(Box2DShapeInstance &p_shape, bool p_shapes_changed = true);
	void rebuild_all_shapes();

	virtual uint64_t modify_mask_bits(uint32_t p_mask) { return p_mask; }
	virtual uint64_t modify_layer_bits(uint32_t p_layer) { return p_layer; }

	virtual void on_added_to_space() {}
	virtual void on_remove_from_space() {}

	Type type = Type::RIGIDBODY;
	Variant user_data = Variant();
	bool is_animatable_body = false;

	Box2DSpace2D *space = nullptr;
	RID rid;
	ObjectID instance_id;
	ObjectID canvas_instance_id;
	bool pickable = true;
	Transform2D current_transform;
	LocalVector<Box2DShapeInstance> shapes;
	PS2DE::BodyMode mode = PS2DE::BodyMode::BODY_MODE_STATIC;

	b2BodyDef body_def = b2DefaultBodyDef();
	b2ShapeDef shape_def = b2DefaultShapeDef();
	b2BodyId body_id = b2_nullBodyId;

	uint16_t generation = 0;

	bool _is_freed = false;
};
