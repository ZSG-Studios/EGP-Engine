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
};
bool valid_geometry(const Geometry &p_geometry);
// Creates a shape (or for CHAIN a chain whose segments are returned in r_segments).
b2ShapeId create_shape(b2BodyId p_body, const b2ShapeDef &p_def, const Geometry &p_geometry, b2ChainId *r_chain);
// Shape-cast proxy (point cloud plus radius) for circle, capsule, box, polygon and
// segment geometry placed at an angle around the query origin.
bool make_proxy(const Geometry &p_geometry, float p_angle, b2ShapeProxy &r_proxy);

void apply_body_def(b2BodyDef &r_def, const Props &p_props);
void apply_shape_def(b2ShapeDef &r_def, const Props &p_props);
b2ChainDef chain_def(const Props &p_props, b2SurfaceMaterial &r_material);
b2JointId create_joint(b2WorldId p_world, JointType p_type, b2BodyId p_a, b2BodyId p_b, const Props &p_props);

void apply_world(b2WorldId p_world, const Props &p_props);
void apply_body(b2BodyId p_body, const Props &p_props, float p_step);
void apply_body_extras(b2BodyId p_body, const Props &p_props, float p_step);
void apply_shape(b2ShapeId p_shape, const Props &p_props);
void apply_chain(b2ChainId p_chain, const Props &p_props);
void apply_joint(b2JointId p_joint, const Props &p_props);

// Finite values, known ids, complete groups (contact tuning is set as hertz, damping
// ratio and speed together).
bool validate_props(FieldSet p_set, const Props &p_props);

struct Value {
	const char *name = "";
	FieldKind kind = FieldKind::FLOAT;
	double v[2] = {};
	uint64_t bits = 0;
};
using Values = std::vector<Value>;
void read_world(b2WorldId p_world, Values &r_values);
void read_body(b2BodyId p_body, Values &r_values);
void read_shape(b2ShapeId p_shape, Values &r_values);
void read_joint(b2JointId p_joint, Values &r_values);

} // namespace egp::box2d
