// SPDX-License-Identifier: MIT
#include "box3d_soft_body_impl_3d.hpp"

#include "../misc/type_conversions.hpp"
#include "../shapes/box3d_sphere_shape_impl_3d.hpp"
#include "../spaces/box3d_space_3d.hpp"
#include "box3d_body_impl_3d.hpp"

#include "core/math/geometry_3d.h"
#include "servers/rendering/rendering_server.h"

Box3DSoftBodyImpl3D::~Box3DSoftBodyImpl3D() {
	set_space(nullptr);
	clear_mesh();
}
void Box3DSoftBodyImpl3D::clear_links() {
	for (auto &link : links) {
		if (B3_IS_NON_NULL(link.joint) && b3Joint_IsValid(link.joint)) {
			b3DestroyJoint(link.joint, true);
		}
		link.joint = b3_nullJointId;
	}
}
void Box3DSoftBodyImpl3D::clear_mesh() {
	clear_links();
	for (auto &node : nodes) {
		if (space) {
			space->forget_object(node.body);
			space->unregister_body(node.body);
		}
		memdelete(node.body);
	}
	nodes.clear();
	faces.clear();
	links.clear();
	visual_map.clear();
	if (particle_shape) {
		memdelete(particle_shape);
		particle_shape = nullptr;
	}
}
void Box3DSoftBodyImpl3D::set_space(Box3DSpace3D *value) {
	if (space == value) {
		return;
	}
	clear_links();
	if (space) {
		space->unregister_soft_body(this);
		for (auto &node : nodes) {
			space->forget_object(node.body);
			space->unregister_body(node.body);
		}
	}
	for (auto &node : nodes) {
		node.body->set_space(value);
	}
	space = value;
	if (space) {
		space->register_soft_body(this);
		for (auto &node : nodes) {
			space->register_body(node.body);
		}
		rebuild_links();
	}
}
void Box3DSoftBodyImpl3D::set_mesh(RID mesh) {
	if (!mesh.is_valid()) {
		clear_mesh();
		return;
	}
	auto *rs = RS::get_singleton();
	ERR_FAIL_NULL(rs);
	ERR_FAIL_COND(rs->mesh_get_surface_count(mesh) < 1);
	ERR_FAIL_COND_MSG(rs->mesh_get_surface(mesh, 0).primitive != RSE::PRIMITIVE_TRIANGLES, "Soft bodies require a triangle mesh on surface zero.");
	Array arrays = rs->mesh_surface_get_arrays(mesh, 0);
	ERR_FAIL_COND(arrays.size() != RSE::ARRAY_MAX);
	Vector<Vector3> vertices = arrays[RSE::ARRAY_VERTEX];
	Vector<int32_t> indices = arrays[RSE::ARRAY_INDEX];
	ERR_FAIL_COND(vertices.size() < 3);
	for (const auto &v : vertices) {
		ERR_FAIL_COND(!v.is_finite());
	}
	const int triangle_elements = indices.is_empty() ? vertices.size() : indices.size();
	ERR_FAIL_COND(triangle_elements % 3 != 0);
	for (int32_t index : indices) {
		ERR_FAIL_INDEX(index, vertices.size());
	}
	// Validate first so a bad replacement does not destroy the previous simulation.
	clear_mesh();
	particle_shape = memnew(Box3DSphereShapeImpl3D);
	particle_shape->set_data(0.01);
	HashMap<Vector3, uint32_t> welded;
	for (const auto &v : vertices) {
		const uint32_t *found = welded.getptr(v);
		if (found) {
			visual_map.push_back(*found);
			continue;
		}
		const uint32_t index = nodes.size();
		welded.insert(v, index);
		visual_map.push_back(index);
		Node node;
		node.rest = v;
		node.body = memnew(Box3DBodyImpl3D);
		node.body->set_rid(rid);
		node.body->set_subindex(index + 1);
		node.body->set_soft_body(this);
		node.body->set_instance_id(instance_id);
		node.body->set_transform(Transform3D(Basis(), transform.xform(v)));
		node.body->set_collision_layer(collision_layer);
		node.body->set_collision_mask(collision_mask);
		node.body->set_ray_pickable(ray_pickable);
		node.body->set_sleep_enabled(false);
		node.body->set_ccd_enabled(true);
		node.body->set_axis_lock(PS3DE::BODY_AXIS_ANGULAR_X, true);
		node.body->set_axis_lock(PS3DE::BODY_AXIS_ANGULAR_Y, true);
		node.body->set_axis_lock(PS3DE::BODY_AXIS_ANGULAR_Z, true);
		node.body->add_shape(particle_shape, Transform3D(), false);
		nodes.push_back(node);
	}
	HashSet<uint64_t> edge_set;
	auto add_edge = [&](uint32_t a, uint32_t b) {
		if (a == b) {
			return;
		}
		if (a > b) {
			SWAP(a, b);
		}
		uint64_t key = (uint64_t(a) << 32) | b;
		if (edge_set.has(key)) {
			return;
		}
		edge_set.insert(key);
		links.push_back({ a, b, transform.basis.xform(nodes[b].rest - nodes[a].rest).length(), b3_nullJointId });
	};
	HashMap<uint64_t, uint32_t> opposite;
	for (int i = 0; i < triangle_elements; i += 3) {
		uint32_t a = visual_map[indices.is_empty() ? i : indices[i]];
		uint32_t b = visual_map[indices.is_empty() ? i + 1 : indices[i + 1]];
		uint32_t c = visual_map[indices.is_empty() ? i + 2 : indices[i + 2]];
		if (a == b || b == c || c == a) {
			continue;
		}
		faces.push_back({ a, b, c });
		add_edge(a, b);
		add_edge(b, c);
		add_edge(c, a);
		// Opposite vertices across a shared edge provide deterministic bending links.
		const uint32_t edge_a[3] = { a, b, c }, edge_b[3] = { b, c, a }, edge_opposite[3] = { c, a, b };
		for (int e = 0; e < 3; ++e) {
			uint64_t key = (uint64_t(MIN(edge_a[e], edge_b[e])) << 32) | MAX(edge_a[e], edge_b[e]);
			if (const uint32_t *other = opposite.getptr(key)) {
				add_edge(*other, edge_opposite[e]);
			} else {
				opposite.insert(key, edge_opposite[e]);
			}
		}
	}
	update_particles();
	for (auto &node : nodes) {
		node.body->set_space(space);
		if (space) {
			space->register_body(node.body);
		}
	}
	rebuild_links();
	update_normals();
}
void Box3DSoftBodyImpl3D::update_particles() {
	if (nodes.is_empty()) {
		return;
	}
	HashSet<uint32_t> pinned_nodes;
	for (int index : pinned) {
		if (index >= 0 && index < int(visual_map.size())) {
			pinned_nodes.insert(visual_map[index]);
		}
	}
	for (uint32_t i = 0; i < nodes.size(); ++i) {
		auto *body = nodes[i].body;
		body->set_mass(total_mass / nodes.size());
		body->set_linear_damping(damping);
		body->set_mode(pinned_nodes.has(i) ? PS3DE::BODY_MODE_STATIC : PS3DE::BODY_MODE_RIGID);
		if (pinned_nodes.has(i)) {
			body->set_linear_velocity(Vector3());
		}
	}
}
void Box3DSoftBodyImpl3D::rebuild_links() {
	clear_links();
	if (!space || nodes.is_empty()) {
		return;
	}
	for (auto &link : links) {
		auto def = b3DefaultDistanceJointDef();
		def.base.bodyIdA = nodes[link.a].body->get_body_id();
		def.base.bodyIdB = nodes[link.b].body->get_body_id();
		if (nodes[link.a].body->get_mode() == PS3DE::BODY_MODE_STATIC && nodes[link.b].body->get_mode() == PS3DE::BODY_MODE_STATIC) {
			continue;
		}
		def.length = MAX(float(link.length * (1 - shrinking)), 0.0001f);
		def.enableSpring = true;
		def.hertz = 0;
		def.dampingRatio = 1;
		link.joint = b3CreateDistanceJoint(space->get_world_id(), &def);
	}
	update_links();
}
void Box3DSoftBodyImpl3D::update_links() {
	for (auto &link : links) {
		if (B3_IS_NON_NULL(link.joint) && b3Joint_IsValid(link.joint)) {
			b3DistanceJoint_SetLength(link.joint, MAX(float(link.length * (1 - shrinking)), 0.0001f));
			b3DistanceJoint_EnableSpring(link.joint, stiffness < 1);
			b3DistanceJoint_SetSpringHertz(link.joint, float(30 * Math::sqrt(stiffness / MAX(real_t(0.001), 1 - stiffness))));
			b3DistanceJoint_SetSpringDampingRatio(link.joint, 1);
		}
	}
}
void Box3DSoftBodyImpl3D::set_transform(const Transform3D &value) {
	ERR_FAIL_COND(!value.is_finite() || Math::is_zero_approx(value.basis.determinant()));
	Transform3D delta = value * transform.affine_inverse();
	transform = value;
	for (auto &node : nodes) {
		auto previous = node.body->get_transform();
		previous.origin = delta.xform(previous.origin);
		node.body->set_transform(previous);
		node.body->set_linear_velocity(delta.basis.xform(node.body->get_linear_velocity()));
	}
	for (auto &link : links) {
		link.length = transform.basis.xform(nodes[link.b].rest - nodes[link.a].rest).length();
	}
	update_links();
}
void Box3DSoftBodyImpl3D::set_instance(uint64_t value) {
	instance_id = value;
	for (auto &node : nodes) {
		node.body->set_instance_id(value);
	}
}
void Box3DSoftBodyImpl3D::set_collision_layer(uint32_t value) {
	collision_layer = value;
	for (auto &node : nodes) {
		node.body->set_collision_layer(value);
	}
}
void Box3DSoftBodyImpl3D::set_collision_mask(uint32_t value) {
	collision_mask = value;
	for (auto &node : nodes) {
		node.body->set_collision_mask(value);
	}
}
void Box3DSoftBodyImpl3D::set_pickable(bool value) {
	ray_pickable = value;
	for (auto &node : nodes) {
		node.body->set_ray_pickable(value);
	}
}
void Box3DSoftBodyImpl3D::set_exception(RID other, bool add) {
	ERR_FAIL_COND(other == rid);
	if (add) {
		exceptions.insert(other);
	} else {
		exceptions.erase(other);
	}
	for (auto &node : nodes) {
		node.body->rebuild_shapes();
	}
}
void Box3DSoftBodyImpl3D::set_total_mass(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value) || value <= 0);
	total_mass = value;
	update_particles();
}
void Box3DSoftBodyImpl3D::set_stiffness(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value) || value < 0 || value > 1);
	stiffness = value;
	update_links();
}
void Box3DSoftBodyImpl3D::set_pressure(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value));
	pressure = value;
}
void Box3DSoftBodyImpl3D::set_damping(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value) || value < 0);
	damping = value;
	update_particles();
}
void Box3DSoftBodyImpl3D::set_drag(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value) || value < 0);
	drag = value;
}
void Box3DSoftBodyImpl3D::set_shrinking(real_t value) {
	ERR_FAIL_COND(!std::isfinite(value) || value < 0 || value > 1);
	shrinking = value;
	update_links();
}
void Box3DSoftBodyImpl3D::set_precision(int value) {
	ERR_FAIL_COND(value < 1 || value > 100);
	precision = value;
}
Vector3 Box3DSoftBodyImpl3D::get_point(int index) const {
	ERR_FAIL_INDEX_V(index, int(visual_map.size()), Vector3());
	return nodes[visual_map[index]].body->get_transform().origin;
}
void Box3DSoftBodyImpl3D::move_point(int index, const Vector3 &value) {
	ERR_FAIL_INDEX(index, int(visual_map.size()));
	ERR_FAIL_COND(!value.is_finite());
	auto *body = nodes[visual_map[index]].body;
	body->set_transform(Transform3D(Basis(), value));
	body->set_linear_velocity(Vector3());
}
void Box3DSoftBodyImpl3D::pin_point(int index, bool value) {
	ERR_FAIL_COND(index < 0);
	// Nodes may request pins before set_mesh during scene construction.
	if (value) {
		pinned.insert(index);
	} else {
		pinned.erase(index);
	}
	update_particles();
	rebuild_links();
}
bool Box3DSoftBodyImpl3D::is_pinned(int index) const {
	return pinned.has(index);
}
void Box3DSoftBodyImpl3D::unpin_all() {
	pinned.clear();
	update_particles();
	rebuild_links();
}
void Box3DSoftBodyImpl3D::apply_point(int index, const Vector3 &value, bool impulse) {
	ERR_FAIL_INDEX(index, int(visual_map.size()));
	ERR_FAIL_COND(!value.is_finite());
	auto *body = nodes[visual_map[index]].body;
	if (impulse) {
		body->apply_central_impulse(value);
	} else {
		body->apply_central_force(value);
	}
}
void Box3DSoftBodyImpl3D::apply_central(const Vector3 &value, bool impulse) {
	ERR_FAIL_COND(!value.is_finite());
	if (nodes.is_empty()) {
		return;
	}
	for (auto &node : nodes) {
		if (impulse) {
			node.body->apply_central_impulse(value / nodes.size());
		} else {
			node.body->apply_central_force(value / nodes.size());
		}
	}
}
void Box3DSoftBodyImpl3D::set_state(PS3DE::BodyState state, const Variant &value) {
	switch (state) {
		case PS3DE::BODY_STATE_TRANSFORM:
			set_transform(value);
			break;
		case PS3DE::BODY_STATE_LINEAR_VELOCITY:
			for (auto &node : nodes) {
				node.body->set_linear_velocity(value);
			}
			break;
		case PS3DE::BODY_STATE_ANGULAR_VELOCITY: {
			Vector3 angular = value;
			ERR_FAIL_COND(!angular.is_finite());
			for (auto &node : nodes) {
				node.body->set_linear_velocity(angular.cross(node.body->get_transform().origin - transform.origin));
			}
			break;
		}
		case PS3DE::BODY_STATE_SLEEPING:
			for (auto &node : nodes) {
				node.body->set_sleeping(value);
			}
			break;
		case PS3DE::BODY_STATE_CAN_SLEEP:
			for (auto &node : nodes) {
				node.body->set_sleep_enabled(value);
			}
			break;
		default:
			ERR_FAIL_MSG("Unknown soft-body state.");
	}
}
Variant Box3DSoftBodyImpl3D::get_state(PS3DE::BodyState state) const {
	if (state == PS3DE::BODY_STATE_TRANSFORM) {
		return transform;
	}
	if (state == PS3DE::BODY_STATE_LINEAR_VELOCITY) {
		Vector3 v;
		for (const auto &node : nodes) {
			v += node.body->get_linear_velocity();
		}
		return nodes.is_empty() ? v : v / nodes.size();
	}
	if (state == PS3DE::BODY_STATE_SLEEPING) {
		for (const auto &node : nodes) {
			if (!node.body->is_sleeping()) {
				return false;
			}
		}
		return true;
	}
	if (state == PS3DE::BODY_STATE_CAN_SLEEP) {
		return !nodes.is_empty() && nodes[0].body->is_sleep_enabled();
	}
	if (state == PS3DE::BODY_STATE_ANGULAR_VELOCITY) {
		Vector3 momentum;
		Basis inertia{ Vector3(), Vector3(), Vector3() };
		for (const auto &node : nodes) {
			Vector3 r = node.body->get_transform().origin - transform.origin;
			momentum += r.cross(node.body->get_linear_velocity());
			inertia.rows[0] += Vector3(r.y * r.y + r.z * r.z, -r.x * r.y, -r.x * r.z);
			inertia.rows[1] += Vector3(-r.x * r.y, r.x * r.x + r.z * r.z, -r.y * r.z);
			inertia.rows[2] += Vector3(-r.x * r.z, -r.y * r.z, r.x * r.x + r.y * r.y);
		}
		return Math::is_zero_approx(inertia.determinant()) ? Vector3() : inertia.inverse().xform(momentum);
	}
	ERR_FAIL_V_MSG(Variant(), "Unknown soft-body state.");
}
AABB Box3DSoftBodyImpl3D::get_bounds() const {
	if (nodes.is_empty()) {
		return AABB();
	}
	AABB bounds(nodes[0].body->get_transform().origin, Vector3());
	for (const auto &node : nodes) {
		bounds.expand_to(node.body->get_transform().origin);
	}
	return bounds;
}
void Box3DSoftBodyImpl3D::update_normals() {
	for (auto &node : nodes) {
		node.normal = Vector3();
		node.area = 0;
	}
	for (const auto &face : faces) {
		Vector3 a = nodes[face.a].body->get_transform().origin, b = nodes[face.b].body->get_transform().origin, c = nodes[face.c].body->get_transform().origin;
		Vector3 cross = (c - a).cross(b - a);
		real_t area = cross.length() / 6;
		for (uint32_t index : { face.a, face.b, face.c }) {
			nodes[index].normal += cross;
			nodes[index].area += area;
		}
	}
	for (auto &node : nodes) {
		node.normal.normalize();
	}
}
void Box3DSoftBodyImpl3D::update_rendering(PhysicsServer3DRenderingServerHandler *handler) {
	ERR_FAIL_NULL(handler);
	update_normals();
	for (uint32_t i = 0; i < visual_map.size(); ++i) {
		const auto &node = nodes[visual_map[i]];
		handler->set_vertex(i, node.body->get_transform().origin);
		handler->set_normal(i, node.normal);
	}
	handler->set_aabb(get_bounds());
}
void Box3DSoftBodyImpl3D::pre_step(float dt) {
	if (nodes.is_empty()) {
		return;
	}
	update_normals();
	real_t volume = 0;
	Vector3 origin = nodes[0].body->get_transform().origin;
	for (const auto &face : faces) {
		volume += (nodes[face.a].body->get_transform().origin - origin).dot((nodes[face.b].body->get_transform().origin - origin).cross(nodes[face.c].body->get_transform().origin - origin)) / 6;
	}
	for (auto &node : nodes) {
		Vector3 force;
		if (space) {
			force += space->compute_wind(node.body, node.normal, node.area);
		}
		if (!Math::is_zero_approx(volume)) {
			force += node.normal * (node.area * pressure / Math::abs(volume));
		}
		const Vector3 velocity = node.body->get_linear_velocity();
		force -= velocity * (drag * node.area * velocity.length());
		node.body->apply_central_force(force);
	}
	// Additional deterministic velocity prediction sweeps controlled by precision.
	// Box3D retains its four contact/joint substeps and handles the resulting contacts.
	if (stiffness <= 0) {
		return;
	}
	LocalVector<Vector3> predicted;
	predicted.resize(nodes.size());
	for (uint32_t i = 0; i < nodes.size(); ++i) {
		predicted[i] = nodes[i].body->get_transform().origin + nodes[i].body->get_linear_velocity() * dt;
	}
	real_t alpha = stiffness == 1 ? real_t(1) : stiffness / precision;
	for (int pass = 0; pass < precision; ++pass) {
		for (const auto &link : links) {
			bool a_dynamic = nodes[link.a].body->get_mode() != PS3DE::BODY_MODE_STATIC;
			bool b_dynamic = nodes[link.b].body->get_mode() != PS3DE::BODY_MODE_STATIC;
			int dynamic_count = int(a_dynamic) + int(b_dynamic);
			if (!dynamic_count) {
				continue;
			}
			Vector3 d = predicted[link.b] - predicted[link.a];
			real_t length = d.length();
			if (length <= CMP_EPSILON) {
				continue;
			}
			Vector3 correction = d * (alpha * (length - link.length * (1 - shrinking)) / (length * dynamic_count));
			if (a_dynamic) {
				predicted[link.a] += correction;
			}
			if (b_dynamic) {
				predicted[link.b] -= correction;
			}
		}
	}
	for (uint32_t i = 0; i < nodes.size(); ++i) {
		if (nodes[i].body->get_mode() != PS3DE::BODY_MODE_STATIC && !nodes[i].body->is_sleeping()) {
			nodes[i].body->set_linear_velocity((predicted[i] - nodes[i].body->get_transform().origin) / dt);
		}
	}
}
bool Box3DSoftBodyImpl3D::intersect_ray(const Vector3 &from, const Vector3 &to, bool backfaces, PS3DT::RayResult &result) const {
	Vector3 delta = to - from;
	real_t best = 1;
	bool hit = false;
	for (uint32_t i = 0; i < faces.size(); ++i) {
		const auto &face = faces[i];
		Vector3 a = nodes[face.a].body->get_transform().origin, b = nodes[face.b].body->get_transform().origin, c = nodes[face.c].body->get_transform().origin;
		Vector3 normal = (c - a).cross(b - a);
		if (!backfaces && normal.dot(delta) >= 0) {
			continue;
		}
		Vector3 point;
		if (!Geometry3D::segment_intersects_triangle(from, to, a, b, c, &point)) {
			continue;
		}
		real_t fraction = delta.length_squared() > 0 ? (point - from).dot(delta) / delta.length_squared() : 0;
		if (fraction > best) {
			continue;
		}
		best = fraction;
		hit = true;
		result.position = point;
		result.normal = normal.normalized();
		result.rid = rid;
		result.collider_id = instance_id;
		result.shape = 0;
		result.face_index = i;
	}
	return hit;
}
