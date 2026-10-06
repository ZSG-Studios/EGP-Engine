/**************************************************************************/
/*  box2d_concave_polygon_shape_2d.cpp                                    */
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
#include "box2d_concave_polygon_shape_2d.h"

#include "../bodies/box2d_collision_object_2d.h"

void Box2DConcavePolygonShape2D::add_to_body(Box2DShapeInstance *p_instance) const {
	Variant::Type type = data.get_type();
	ERR_FAIL_COND(type != Variant::PACKED_VECTOR2_ARRAY);
	PackedVector2Array arr = data;
	ERR_FAIL_COND(arr.size() % 2);

	Transform2D shape_transform = p_instance->get_global_transform();
	b2ShapeDef shape_def = p_instance->get_shape_def();

	for (int i = 0; i < arr.size() - 1; i += 2) {
		b2Vec2 point_a = to_box2d(shape_transform.xform(arr[i]));
		b2Vec2 point_b = to_box2d(shape_transform.xform(arr[i + 1]));
		b2Segment segment;
		segment.point1 = point_a;
		segment.point2 = point_b;
		b2ShapeId id = b2CreateSegmentShape(p_instance->get_collision_object()->get_body_id(), &shape_def, &segment);
		p_instance->add_shape_id(id);
	}
}

int Box2DConcavePolygonShape2D::overlap(const OverlapQuery &p_query, const Transform2D &p_transform, LocalVector<ShapeOverlap> &p_results) const {
	Variant::Type type = data.get_type();
	ERR_FAIL_COND_V(type != Variant::PACKED_VECTOR2_ARRAY, 0);
	PackedVector2Array arr = data;
	ERR_FAIL_COND_V(arr.size() % 2, 0);

	OverlapQuery query = p_query;
	LocalVector<ShapeOverlap> results;

	Transform2D shape_transform = p_transform;
	b2Capsule capsule;
	capsule.radius = 0.0;

	for (int i = 0; i < arr.size() - 1; i += 2) {
		capsule.center1 = to_box2d(shape_transform.xform(arr[i]));
		capsule.center2 = to_box2d(shape_transform.xform(arr[i + 1]));

		int count = box2d_overlap_shape(capsule, query, results);
		query.max_results -= count;
		if (query.max_results <= 0) {
			break;
		}
	}

	// De-duplicate overlaps
	uint32_t i = 0;
	while (i < results.size()) {
		uint32_t j = i + 1;
		while (j < results.size()) {
			if (results[j] == results[i]) {
				results.remove_at(j);
			} else {
				++j;
			}
		}
		++i;
	}

	for (ShapeOverlap &overlap : results) {
		p_results.push_back(overlap);
	}

	return results.size();
}
