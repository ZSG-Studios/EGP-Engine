// SPDX-License-Identifier: MIT
#pragma once

// Table-driven exposure of Box3D: every world, body, shape (material, filter,
// events) and joint field addressable by a stable snake_case name, applied either
// to a Box3D definition at creation or through Box3D's runtime setters, and read
// back the same way. No Godot or networking dependency; used by the deterministic
// world and its bindings.

#include "box3d/box3d.h"

#include <cstdint>
#include <vector>

namespace egp::box3d {

enum class FieldKind : uint8_t { FLOAT,
	BOOL,
	INT,
	U64,
	VEC3,
	QUAT };

struct FieldInfo {
	const char *name;
	uint16_t id;
	FieldKind kind;
};

// One field value. Scalars use v[0]; vectors x,y,z; quaternions x,y,z,w. U64 values
// keep full precision in bits.
struct Prop {
	uint16_t id = 0;
	double v[4] = {};
	uint64_t bits = 0;
};
using Props = std::vector<Prop>;

enum class FieldSet : uint8_t { WORLD,
	BODY,
	SHAPE,
	JOINT };

// Lookup by name; nullptr when unknown. all_fields lists a set (documentation, readback).
const FieldInfo *find_field(FieldSet p_set, const char *p_name);
const std::vector<FieldInfo> &all_fields(FieldSet p_set);

// Joint types exposed by name. The first three keep the original deterministic ids.
// GENERIC is EGP's 6DOF joint: per-axis limits, springs and motors.
enum class JointType : uint32_t { DISTANCE = 0,
	SPHERICAL = 1,
	PRISMATIC = 2,
	MOTOR = 3,
	REVOLUTE = 4,
	WELD = 5,
	WHEEL = 6,
	FILTER = 7,
	PARALLEL = 8,
	GENERIC = 9 };
constexpr uint32_t JOINT_TYPE_COUNT = 10;
bool joint_type_from_name(const char *p_name, JointType &r_type);
const char *joint_type_name(JointType p_type);
b3JointType box3d_joint_type(JointType p_type);

// Shape geometry. Every shape takes a local offset (center) and rotation. Mesh,
// height-field and baked compound data are owned by the world that created the shape
// (Box3D references them); hulls are copied by Box3D. Height fields and compounds are
// static only.
enum class ShapeType : uint8_t { SPHERE,
	CAPSULE,
	BOX,
	HULL,
	MESH,
	HEIGHT_FIELD,
	COMPOUND };
bool shape_type_from_name(const char *p_name, ShapeType &r_type);
const char *shape_type_name(ShapeType p_type);
struct Geometry {
	ShapeType type = ShapeType::SPHERE;
	float radius = 0.5f;
	b3Vec3 center = {};
	b3Quat rotation = { {}, 1.0f };
	// Capsule segment end points (local, before center/rotation).
	b3Vec3 center1 = { 0.0f, -0.5f, 0.0f };
	b3Vec3 center2 = { 0.0f, 0.5f, 0.0f };
	b3Vec3 half_extents = { 0.5f, 0.5f, 0.5f };
	b3Vec3 scale = { 1.0f, 1.0f, 1.0f };
	// Hull points, or mesh vertices with three indices per triangle.
	std::vector<b3Vec3> points;
	std::vector<int32_t> indices;
	// Height field: count_x * count_z heights (row-major along x), optional holes per
	// cell ((count_x - 1) * (count_z - 1), non-zero is a hole), scaled by scale.
	std::vector<float> heights;
	std::vector<uint8_t> holes;
	int32_t count_x = 0;
	int32_t count_z = 0;
	// Per-triangle (mesh) or per-cell (height field) material: an index into the shape's
	// material table, where 0 is the shape's own material and 1.. are extra_materials.
	std::vector<uint8_t> material_indices;
	std::vector<b3SurfaceMaterial> extra_materials;
	// Baked compound (static): sphere, capsule, box, hull and mesh children, each with
	// its own placement and material (mesh children may also carry per-triangle tables).
	std::vector<Geometry> children;
	b3SurfaceMaterial material = b3DefaultSurfaceMaterial();
};
constexpr size_t MAX_COMPOUND_CHILDREN = 4096;
bool valid_geometry(const Geometry &p_geometry);
bool valid_material(const b3SurfaceMaterial &p_material);
// Height fields and compounds require a static body (and compounds a non-sensor shape).
bool static_only(ShapeType p_type);

// Owns Box3D geometry data that shapes reference for the lifetime of a world.
class GeometryStore {
	std::vector<b3MeshData *> meshes;
	std::vector<b3HeightFieldData *> height_fields;
	std::vector<b3CompoundData *> compounds;

public:
	GeometryStore() = default;
	GeometryStore(const GeometryStore &) = delete;
	GeometryStore &operator=(const GeometryStore &) = delete;
	~GeometryStore() { clear(); }
	// Only call once no world references the data any more.
	void clear();
	b3ShapeId create_shape(b3BodyId p_body, b3ShapeDef p_def, const Geometry &p_geometry);
};

// Shape-cast proxy (point cloud plus radius) for sphere, capsule, box and hull
// geometry, placed at a rotation around the query origin.
bool make_proxy(const Geometry &p_geometry, b3Quat p_rotation, std::vector<b3Vec3> &r_points, b3ShapeProxy &r_proxy);

// Creation: apply fields onto definitions. Inapplicable fields are skipped
// (validated earlier by validate_props).
void apply_body_def(b3BodyDef &r_def, const Props &p_props);
void apply_shape_def(b3ShapeDef &r_def, const Props &p_props);
b3JointId create_joint(b3WorldId p_world, JointType p_type, b3BodyId p_a, b3BodyId p_b, const Props &p_props);

// Runtime: apply fields through Box3D setters. p_step is the tick duration (kinematic
// target transforms default to reaching the target in one tick).
void apply_world(b3WorldId p_world, const Props &p_props);
void apply_body(b3BodyId p_body, const Props &p_props, float p_step);
// Body fields Box3D has no definition slot for (mass override, hit events, kinematic
// target); applied right after the body's shapes are created.
void apply_body_extras(b3BodyId p_body, const Props &p_props, float p_step);
// p_material_index >= 0 changes one entry of a mesh or height field's material table
// (0 is the shape's own material) instead of the shape material.
void apply_shape(b3ShapeId p_shape, const Props &p_props, int32_t p_material_index = -1);
void apply_joint(b3JointId p_joint, const Props &p_props);

// Finite values, unit quaternions, known ids and every range Box3D asserts on
// (non-negative stiffness, damping, limits and thresholds; angles; positive lengths).
bool validate_props(FieldSet p_set, const Props &p_props);
// Merged lower/upper pairs (props over the joint's current values, or over the type's
// defaults when p_existing is null) stay ordered.
bool joint_limits_ordered(JointType p_type, const b3JointId *p_existing, const Props &p_props);
// Queries used by batch validation.
bool props_body_type(const Props &p_props, int &r_type);
bool props_sensor(const Props &p_props);
bool props_material(const Props &p_props);
// A material from shape fields over a base (compound children, extra materials).
b3SurfaceMaterial material_from(const Props &p_props, b3SurfaceMaterial p_base);

// Readback: every readable field plus derived state (mass, joint angles, forces).
struct Value {
	const char *name = "";
	FieldKind kind = FieldKind::FLOAT;
	double v[4] = {};
	uint64_t bits = 0;
};
using Values = std::vector<Value>;
void read_world(b3WorldId p_world, Values &r_values);
void read_body(b3BodyId p_body, Values &r_values);
void read_shape(b3ShapeId p_shape, Values &r_values);
void read_material(const b3SurfaceMaterial &p_material, Values &r_values);
void read_joint(b3JointId p_joint, Values &r_values);

} // namespace egp::box3d
