// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "../box2d_globals.h"

#include "modules/box2d/precompiled.h"

using namespace PhysicsServer2DEnums;

class Box2DSpace2D;
class CastHit;
class ShapeOverlap;

class Box2DDirectSpaceState2D : public PhysicsDirectSpaceState2D {
	GDCLASS(Box2DDirectSpaceState2D, PhysicsDirectSpaceState2D);

public:
	HashSet<RID> query_exclude;
	bool is_body_excluded_from_query(RID rid) const { return query_exclude.has(rid); }
	bool intersect_ray(const PS2DT::RayParameters &, PS2DT::RayResult &) override;
	int intersect_point(const PS2DT::PointParameters &, PS2DT::ShapeResult *, int) override;
	int intersect_shape(const PS2DT::ShapeParameters &, PS2DT::ShapeResult *, int) override;
	bool cast_motion(const PS2DT::ShapeParameters &, real_t &, real_t &) override;
	bool collide_shape(const PS2DT::ShapeParameters &, Vector2 *, int, int &) override;
	bool rest_info(const PS2DT::ShapeParameters &, PS2DT::ShapeRestInfo *) override;
	Box2DDirectSpaceState2D() = default;
	Box2DDirectSpaceState2D(Box2DSpace2D *p_space) :
			space(p_space) {}

	bool _intersect_ray(const Vector2 &p_from, const Vector2 &p_to, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, bool p_hit_from_inside, PS2DT::RayResult *p_result);
	int32_t _intersect_point(const Vector2 &p_position, uint64_t p_canvas_instance_id, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, bool p_pick_point, PS2DT::ShapeResult *p_results, int32_t p_max_results);
	int32_t _intersect_shape(RID p_shape_rid, const Transform2D &p_transform, const Vector2 &p_motion, real_t p_margin, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, PS2DT::ShapeResult *p_result, int32_t p_max_results);
	bool _cast_motion(RID p_shape_rid, const Transform2D &p_transform, const Vector2 &p_motion, real_t p_margin, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, real_t *p_closest_safe, real_t *p_closest_unsafe);
	bool _collide_shape(RID p_shape_rid, const Transform2D &p_transform, const Vector2 &p_motion, real_t p_margin, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, void *p_results, int32_t p_max_results, int32_t *p_result_count);
	bool _rest_info(RID p_shape_rid, const Transform2D &p_transform, const Vector2 &p_motion, real_t p_margin, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, PS2DT::ShapeRestInfo *p_rest_info);

	Dictionary cast_shape(const Ref<PhysicsShapeQueryParameters2D> &p_parameters);
	TypedArray<Dictionary> cast_shape_all(const Ref<PhysicsShapeQueryParameters2D> &p_parameters, int32_t p_max_results = 32);

	static Object *get_instance_hack(uint64_t id) { return ObjectDB::get_instance(ObjectID(id)); }
	b2QueryFilter make_filter(uint64_t p_collision_mask, bool p_collide_bodies, bool p_collide_areas);

private:
	static void _bind_methods();

	Box2DSpace2D *space = nullptr;
};
