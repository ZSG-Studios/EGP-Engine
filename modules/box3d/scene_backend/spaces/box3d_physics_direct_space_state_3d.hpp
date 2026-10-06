// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "precompiled.hpp"
#pragma once

class Box3DShapedObjectImpl3D;
class Box3DSpace3D;

// Query overrides: raycasts and shape queries against one Box3DSpace3D. Box3D's own filter
// (b3QueryFilter) has no native per-query RID-exclude list, so a side-channel HashSet<RID>
// is checked from inside each b3*ResultFcn callback (see Box3DQueryFilter3D).
class Box3DPhysicsDirectSpaceState3D final : public PhysicsDirectSpaceState3D {
	GDCLASS(Box3DPhysicsDirectSpaceState3D, PhysicsDirectSpaceState3D)

public:
	void set_space(Box3DSpace3D *p_space) { space = p_space; }

	Box3DSpace3D *get_space() const { return space; }

	bool _intersect_ray(
			const Vector3 &p_from,
			const Vector3 &p_to,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			bool p_hit_from_inside,
			bool p_hit_back_faces,
			bool p_pick_ray,
			PS3DT::RayResult *p_result);

	int32_t _intersect_point(
			const Vector3 &p_position,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			PS3DT::ShapeResult *p_results,
			int32_t p_max_results);

	int32_t _intersect_shape(
			const RID &p_shape_rid,
			const Transform3D &p_transform,
			const Vector3 &p_motion,
			real_t p_margin,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			PS3DT::ShapeResult *p_results,
			int32_t p_max_results);

	bool _cast_motion(
			const RID &p_shape_rid,
			const Transform3D &p_transform,
			const Vector3 &p_motion,
			real_t p_margin,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			float *p_closest_safe,
			float *p_closest_unsafe,
			PS3DT::ShapeRestInfo *p_info);

	bool _collide_shape(
			const RID &p_shape_rid,
			const Transform3D &p_transform,
			const Vector3 &p_motion,
			real_t p_margin,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			void *p_results,
			int32_t p_max_results,
			int32_t *p_result_count);

	bool _rest_info(
			const RID &p_shape_rid,
			const Transform3D &p_transform,
			const Vector3 &p_motion,
			real_t p_margin,
			uint32_t p_collision_mask,
			bool p_collide_with_bodies,
			bool p_collide_with_areas,
			PS3DT::ShapeRestInfo *p_info);

	Vector3 _get_closest_point_to_object_volume(const RID &p_object, const Vector3 &p_point) const;

	// Used by Box3DPhysicsServer3DExtension::_body_test_motion.
	bool test_body_motion(
			Box3DShapedObjectImpl3D &p_body,
			const Transform3D &p_transform,
			const Vector3 &p_motion,
			real_t p_margin,
			int32_t p_max_collisions,
			bool p_recovery_as_collision,
			PS3DT::MotionResult *p_result) const;

	HashSet<RID> query_exclude;
	HashSet<ObjectID> motion_exclude_objects;
	bool is_body_excluded_from_query(RID rid) const { return query_exclude.has(rid); }
	bool intersect_ray(const PS3DT::RayParameters &p, PS3DT::RayResult &r) override;
	int intersect_point(const PS3DT::PointParameters &p, PS3DT::ShapeResult *r, int max) override;
	int intersect_shape(const PS3DT::ShapeParameters &p, PS3DT::ShapeResult *r, int max) override;
	bool cast_motion(const PS3DT::ShapeParameters &p, real_t &safe, real_t &unsafe, PS3DT::ShapeRestInfo *r = nullptr) override;
	bool collide_shape(const PS3DT::ShapeParameters &p, Vector3 *r, int max, int &count) override;
	bool rest_info(const PS3DT::ShapeParameters &p, PS3DT::ShapeRestInfo *r) override;
	Vector3 get_closest_point_to_object_volume(RID rid, const Vector3 point) const override { return _get_closest_point_to_object_volume(rid, point); }

protected:
	static void _bind_methods() {}

private:
	Box3DSpace3D *space = nullptr;
};
