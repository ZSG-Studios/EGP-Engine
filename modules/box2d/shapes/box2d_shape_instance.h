/**************************************************************************/
/*  box2d_shape_instance.h                                                */
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

// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_shape_2d.h"

#include "modules/box2d/precompiled.h"

class Box2DShape2D;
class Box2DCollisionObject2D;

class Box2DShapeInstance {
	friend class Box2DLocalReplay;

public:
	explicit Box2DShapeInstance(Box2DCollisionObject2D *p_object,
			Box2DShape2D *p_shape,
			const Transform2D &p_transform,
			bool p_disabled);

	Box2DShapeInstance() = default;
	Box2DShapeInstance(const Box2DShapeInstance &) = delete;
	Box2DShapeInstance &operator=(const Box2DShapeInstance &) = delete;
	Box2DShapeInstance(Box2DShapeInstance &&other) noexcept;
	Box2DShapeInstance &operator=(Box2DShapeInstance &&other) noexcept;

	~Box2DShapeInstance();

	void set_shape(Box2DShape2D *p_shape);
	Box2DShape2D *get_shape() const { return shape; }

	void set_index(int p_index) {
		index = p_index;
		// LocalVector growth relocates instances; every live solver shape must follow.
		for (b2ShapeId id : shape_ids) {
			if (b2Shape_IsValid(id)) {
				b2Shape_SetUserData(id, this);
			}
		}
	}
	int get_index() const { return index; }

	void set_transform(const Transform2D &p_transform) { transform = p_transform; }
	Transform2D get_transform() const { return transform; }

	void set_disabled(bool p_disabled) { disabled = p_disabled; }
	bool get_disabled() const { return disabled; }

	void set_one_way_direction(const Vector2 &direction) { one_way_direction = direction; }
	void set_one_way_collision(bool p_one_way) { one_way_collision = p_one_way; }
	_FORCE_INLINE_ bool has_one_way_collision() const { return one_way_collision; }

	void set_one_way_collision_margin(real_t p_margin) { one_way_collision_margin = p_margin; }
	real_t get_one_way_collision_margin() const { return one_way_collision_margin; }

	bool should_filter_one_way_collision(const Vector2 &p_motion, const Vector2 &p_normal, real_t p_depth) const;
	bool should_filter_one_way_collision(const Vector2 &p_normal) const;

	Transform2D get_global_transform() const;
	Transform2D get_global_transform_with_parent_transform(const Transform2D &p_parent_transform) const;
	b2ShapeDef get_shape_def();

	void build();

	void add_shape_id(b2ShapeId p_id) { shape_ids.push_back(p_id); }
	const LocalVector<b2ShapeId> &get_shape_ids() { return shape_ids; }
	void clear_shape_ids() { shape_ids.clear(); }

	bool is_separation_ray() const { return shape_type == PS2DE::SHAPE_SEPARATION_RAY; }

	Box2DCollisionObject2D *get_collision_object() const { return object; }

private:
	Vector2 get_one_way_normal() const;

	Box2DCollisionObject2D *object = nullptr;
	Box2DShape2D *shape = nullptr;
	PS2DE::ShapeType shape_type = PS2DE::SHAPE_INVALID;

	LocalVector<b2ShapeId> shape_ids;

	int index = -1;
	Transform2D transform;
	bool disabled = false;
	bool one_way_collision = false;
	Vector2 one_way_direction = Vector2(0, 1);
	real_t one_way_collision_margin = 0.0;
};
