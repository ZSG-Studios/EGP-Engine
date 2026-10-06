// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_convex_polygon_shape_2d.h"

#include "../bodies/box2d_collision_object_2d.h"

#include "modules/box2d/precompiled.h"

void Box2DConvexPolygonShape2D::add_to_body(Box2DShapeInstance *p_instance) const {
	b2Polygon shape;
	if (!make_polygon(p_instance->get_global_transform(), data, shape)) {
		return;
	}
	b2ShapeDef shape_def = p_instance->get_shape_def();
	b2ShapeId id = b2CreatePolygonShape(p_instance->get_collision_object()->get_body_id(), &shape_def, &shape);
	p_instance->add_shape_id(id);
}

int Box2DConvexPolygonShape2D::cast(const CastQuery &p_query, const Transform2D &p_transform, LocalVector<CastHit> &p_results) const {
	b2Polygon shape;
	if (!make_polygon(p_transform, data, shape)) {
		return 0;
	}
	return box2d_cast_shape(shape, p_query, p_results);
}

int Box2DConvexPolygonShape2D::overlap(const OverlapQuery &p_query, const Transform2D &p_transform, LocalVector<ShapeOverlap> &p_results) const {
	b2Polygon shape;
	if (!make_polygon(p_transform, data, shape)) {
		return 0;
	}
	return box2d_overlap_shape(shape, p_query, p_results);
}

bool Box2DConvexPolygonShape2D::make_polygon(const Transform2D &p_transform, const Variant &p_data, b2Polygon &p_polygon) {
	Variant::Type type = p_data.get_type();
#ifdef REAL_T_IS_DOUBLE
	ERR_FAIL_COND_V_MSG(type != Variant::PACKED_VECTOR2_ARRAY && type != Variant::PACKED_FLOAT64_ARRAY, false, "Box2D: Convex polygon data must be a vector array or a packed float array matching engine precision.");
#else
	ERR_FAIL_COND_V_MSG(type != Variant::PACKED_VECTOR2_ARRAY && type != Variant::PACKED_FLOAT32_ARRAY, false, "Box2D: Convex polygon data must be a vector array or a packed float array matching engine precision.");
#endif

	ERR_FAIL_COND_V_MSG(!p_transform.is_finite(), false, "Box2D: Convex polygon transform must be finite.");
	const bool vector_format = type == Variant::PACKED_VECTOR2_ARRAY;
	PackedVector2Array vector_points;
#ifdef REAL_T_IS_DOUBLE
	PackedFloat64Array packed_points;
#else
	PackedFloat32Array packed_points;
#endif
	int point_count;
	if (vector_format) {
		vector_points = p_data;
		point_count = vector_points.size();
	} else {
		packed_points = p_data;
		ERR_FAIL_COND_V_MSG(packed_points.size() % 4 != 0, false, "Box2D: Packed convex polygon data must contain complete point/normal tuples of four floats.");
		point_count = packed_points.size() / 4;
	}

	ERR_FAIL_COND_V_MSG(point_count < 3, false, "Box2D: Convex polygons require at least three points.");

	ERR_FAIL_COND_V_MSG(
			point_count > B2_MAX_POLYGON_VERTICES,
			false,
			"Box2D: Convex polygons cannot have more than " + itos(B2_MAX_POLYGON_VERTICES) + " vertices");

	// Validate the count before filling bounded native storage. Packed data stores
	// (point.x, point.y, normal.x, normal.y); Box2D recomputes normals from the hull.
	b2Vec2 points[B2_MAX_POLYGON_VERTICES];
	for (int i = 0; i < point_count; i++) {
		Vector2 point;
		if (vector_format) {
			point = vector_points[i];
		} else {
			point = Vector2(packed_points[4 * i], packed_points[4 * i + 1]);
			const Vector2 normal(packed_points[4 * i + 2], packed_points[4 * i + 3]);
			ERR_FAIL_COND_V_MSG(!normal.is_finite(), false, "Box2D: Packed convex polygon normals must be finite.");
		}
		points[i] = to_box2d(p_transform.xform(point));
		ERR_FAIL_COND_V_MSG(!b2IsValidVec2(points[i]) || Math::abs(points[i].x) > B2_HUGE || Math::abs(points[i].y) > B2_HUGE, false, "Box2D: Convex polygon transformed points must be finite and inside native coordinate bounds.");
	}

	b2Hull hull = b2ComputeHull(points, point_count);
	ERR_FAIL_COND_V_MSG(!b2ValidateHull(&hull), false, "Box2D: Failed to compute a valid convex hull; check polygon scale and noncollinear points.");

	p_polygon = b2MakePolygon(&hull, 0.0);

	return true;
}
