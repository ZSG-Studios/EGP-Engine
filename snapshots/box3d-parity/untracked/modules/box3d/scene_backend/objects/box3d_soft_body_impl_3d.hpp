// SPDX-License-Identifier: MIT
#pragma once

#include "box3d_object_impl_3d.hpp"

class Box3DBodyImpl3D;
class Box3DSphereShapeImpl3D;

// A deformable mesh whose nodes and elastic links use the owning Box3D world.
// Visual vertices weld to simulation nodes in first-occurrence order.
class Box3DSoftBodyImpl3D final : public Box3DObjectImpl3D {
public:
	~Box3DSoftBodyImpl3D() override;
	void set_space(Box3DSpace3D *) override;
	void set_mesh(RID);
	void set_transform(const Transform3D &);
	Transform3D get_transform() const { return transform; }
	void set_instance(uint64_t);
	void set_collision_layer(uint32_t) override;
	void set_collision_mask(uint32_t) override;
	void set_pickable(bool);
	void set_exception(RID, bool);
	const HashSet<RID> &get_exceptions() const { return exceptions; }
	void set_total_mass(real_t);
	real_t get_total_mass() const { return total_mass; }
	void set_stiffness(real_t);
	real_t get_stiffness() const { return stiffness; }
	void set_pressure(real_t);
	real_t get_pressure() const { return pressure; }
	void set_damping(real_t);
	real_t get_damping() const { return damping; }
	void set_drag(real_t);
	real_t get_drag() const { return drag; }
	void set_shrinking(real_t);
	real_t get_shrinking() const { return shrinking; }
	void set_precision(int);
	int get_precision() const { return precision; }
	void move_point(int, const Vector3 &);
	Vector3 get_point(int) const;
	void pin_point(int, bool);
	bool is_pinned(int) const;
	void unpin_all();
	void apply_point(int, const Vector3 &, bool);
	void apply_central(const Vector3 &, bool);
	void set_state(PS3DE::BodyState, const Variant &);
	Variant get_state(PS3DE::BodyState) const;
	AABB get_bounds() const;
	void update_rendering(PhysicsServer3DRenderingServerHandler *);
	void pre_step(float);
	bool intersect_ray(const Vector3 &, const Vector3 &, bool, PS3DT::RayResult &) const;

private:
	struct Node {
		Box3DBodyImpl3D *body = nullptr;
		Vector3 rest;
		Vector3 normal;
		real_t area = 0;
	};
	struct Face {
		uint32_t a, b, c;
	};
	struct Link {
		uint32_t a, b;
		real_t length;
		b3JointId joint = b3_nullJointId;
	};
	void clear_mesh();
	void clear_links();
	void rebuild_links();
	void update_links();
	void update_normals();
	void update_particles();
	LocalVector<Node> nodes;
	LocalVector<Face> faces;
	LocalVector<Link> links;
	LocalVector<uint32_t> visual_map;
	HashSet<int> pinned;
	HashSet<RID> exceptions;
	Box3DSphereShapeImpl3D *particle_shape = nullptr;
	Transform3D transform;
	real_t total_mass = 1, stiffness = 0.5, pressure = 0, damping = 0.01, drag = 0, shrinking = 0;
	int precision = 5;
};
