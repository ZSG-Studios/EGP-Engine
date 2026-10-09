// SPDX-License-Identifier: MIT
#pragma once

// Table-driven exposure of Box2D: every world, body, shape (material, filter,
// events) and joint field addressable by a stable snake_case name, applied either
// to a Box2D definition at creation or through Box2D's runtime setters, and read
// back the same way. Rotations are angles in radians, converted with Box2D's own
// deterministic trigonometry. No Godot or networking dependency.

#include "box2d/box2d.h"

#include <cstdint>
#include <vector>

namespace egp::box2d {

enum class FieldKind : uint8_t { FLOAT,
	BOOL,
	INT,
	U64,
	VEC2 };

struct FieldInfo {
	const char *name;
	uint16_t id;
	FieldKind kind;
};

// One field value. Scalars use v[0]; vectors x,y. U64 values keep full precision in bits.
struct Prop {
	uint16_t id = 0;
	double v[2] = {};
	uint64_t bits = 0;
};
using Props = std::vector<Prop>;

enum class FieldSet : uint8_t { WORLD,
	BODY,
	SHAPE,
	JOINT };

const FieldInfo *find_field(FieldSet p_set, const char *p_name);
const std::vector<FieldInfo> &all_fields(FieldSet p_set);

// Box2D joint types, in b2JointType order.
enum class JointType : uint32_t { DISTANCE = 0,
	FILTER = 1,
	MOTOR = 2,
	PRISMATIC = 3,
	REVOLUTE = 4,
	WELD = 5,
	WHEEL = 6 };
constexpr uint32_t JOINT_TYPE_COUNT = 7;
bool joint_type_from_name(const char *p_name, JointType &r_type);
const char *joint_type_name(JointType p_type);

// Shape geometry. Every shape takes a local offset (center) and angle. A chain is a
// run of one-sided segments (a loop or an open line) sharing one shape index.
enum class ShapeType : uint8_t { CIRCLE,
	CAPSULE,
	BOX,
	POLYGON,
	SEGMENT,
	CHAIN };
bool shape_type_from_name(const char *p_name, ShapeType &r_type);
const char *shape_type_name(ShapeType p_type);
struct Geometry {
	ShapeType type = ShapeType::CIRCLE;
	float radius = 0.5f;
	b2Vec2 center = { 0.0f, 0.0f };
	float angle = 0.0f;
	// Capsule centres or segment end points (local, before center/angle).
	b2Vec2 point_a = { 0.0f, -0.5f };
	b2Vec2 point_b = { 0.0f, 0.5f };
	b2Vec2 half_extents = { 0.5f, 0.5f };
	// Polygon hull points (up to 8) or chain points (4 or more; an open chain's first
	// and last points are ghosts).
	std::vector<b2Vec2> points;
	bool loop = false;
	// Chain materials: empty (one material from the shape fields) or one per segment
	// (points for a loop, points - 3 for an open chain, whose end points are ghosts).
	std::vector<b2SurfaceMaterial> materials;
};
size_t chain_segments(const Geometry &p_geometry);
// The per-point table Box2D's chain definition takes, built from per-segment materials
// (an open chain's ghost points repeat the end segments).
std::vector<b2SurfaceMaterial> chain_point_materials(const Geometry &p_geometry);
// A chain's material table: one shared entry, or one per segment (read and written on
// the segment shapes; Box2D's chain material accessors assert on open chains).
int chain_material_count(b2ChainId p_chain);
b2SurfaceMaterial chain_material(b2ChainId p_chain, int p_index);
bool valid_geometry(const Geometry &p_geometry);
bool valid_material(const b2SurfaceMaterial &p_material);
// Creates a shape (or for CHAIN a chain whose segments are returned in r_segments).
b2ShapeId create_shape(b2BodyId p_body, const b2ShapeDef &p_def, const Geometry &p_geometry, b2ChainId *r_chain);
// Shape-cast proxy (point cloud plus radius) for circle, capsule, box, polygon and
// segment geometry placed at an angle around the query origin.
bool make_proxy(const Geometry &p_geometry, float p_angle, b2ShapeProxy &r_proxy);

void apply_body_def(b2BodyDef &r_def, const Props &p_props);
void apply_shape_def(b2ShapeDef &r_def, const Props &p_props);
b2ChainDef chain_def(const Props &p_props, b2SurfaceMaterial &r_material);
b2JointId create_joint(b2WorldId p_world, JointType p_type, b2BodyId p_a, b2BodyId p_b, const Props &p_props);

// Box2D has no getters for contact tuning or the speculative flag: the deterministic
// world mirrors them (and carries them in its snapshots) so partial updates and
// readback stay exact on every peer.
struct WorldTuning {
	float contact_hertz = 0.0f;
	float contact_damping_ratio = 0.0f;
	float contact_speed = 0.0f;
	bool speculative = true;
};
WorldTuning default_tuning();
void apply_world(b2WorldId p_world, const Props &p_props, WorldTuning &r_tuning);
void apply_body(b2BodyId p_body, const Props &p_props, float p_step);
void apply_body_extras(b2BodyId p_body, const Props &p_props, float p_step);
void apply_shape(b2ShapeId p_shape, const Props &p_props);
// p_material_index >= 0 changes one entry of the chain's material table.
void apply_chain(b2ChainId p_chain, const Props &p_props, int32_t p_material_index = -1);
void apply_joint(b2JointId p_joint, const Props &p_props);

// Finite values, known ids and every range Box2D asserts on (non-negative damping,
// stiffness, thresholds and materials; positive lengths; revolute limits within 0.99 PI).
bool validate_props(FieldSet p_set, const Props &p_props);
// Merged lower/upper pairs (props over the joint's current values, or the type's
// defaults when p_existing is null) stay ordered.
bool joint_limits_ordered(JointType p_type, const b2JointId *p_existing, const Props &p_props);
bool props_material(const Props &p_props);
// The props without surface material fields (filter and event flags only).
Props without_material(const Props &p_props);
b2SurfaceMaterial material_from(const Props &p_props, b2SurfaceMaterial p_base);

struct Value {
	const char *name = "";
	FieldKind kind = FieldKind::FLOAT;
	double v[2] = {};
	uint64_t bits = 0;
};
using Values = std::vector<Value>;
void read_world(b2WorldId p_world, const WorldTuning &p_tuning, Values &r_values);
void read_material(const b2SurfaceMaterial &p_material, Values &r_values);
void read_body(b2BodyId p_body, Values &r_values);
void read_shape(b2ShapeId p_shape, Values &r_values);
void read_joint(b2JointId p_joint, Values &r_values);

} // namespace egp::box2d
