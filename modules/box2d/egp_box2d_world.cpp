// SPDX-License-Identifier: MIT
#include "egp_box2d_world.h"

#include "core/object/class_db.h"

#include <cstdio>
#include <cstring>

// Box2D solver tasks on the engine WorkerThreadPool (defined by the 2D physics server).
void *enqueue_task_callback(b2TaskCallback *p_task, void *p_task_context, void *p_user_context);
void finish_task_callback(void *p_task, void *p_user_context);

namespace {
namespace bx = egp::box2d;

Error to_error(bx::Result r) {
	switch (r) {
		case bx::Result::OK:
			return OK;
		case bx::Result::NOT_CONFIGURED:
			return ERR_UNCONFIGURED;
		case bx::Result::PENDING_COMMANDS:
			return ERR_BUSY;
		case bx::Result::LIMIT_REACHED:
			return ERR_OUT_OF_MEMORY;
		case bx::Result::INVALID_SNAPSHOT:
			return ERR_FILE_CORRUPT;
		case bx::Result::DUPLICATE_COMMAND:
			return ERR_ALREADY_EXISTS;
		case bx::Result::INVALID_BATCH:
			return ERR_INVALID_DATA;
		default:
			return ERR_INVALID_PARAMETER;
	}
}

bool valid_ids(int64_t entity, int64_t sequence) {
	return entity > 0 && sequence >= 0 && uint64_t(sequence) <= UINT32_MAX;
}

b2Vec2 vec(const Vector2 &v) {
	return { float(v.x), float(v.y) };
}

Variant::Type variant_type(bx::FieldKind kind) {
	switch (kind) {
		case bx::FieldKind::FLOAT:
			return Variant::FLOAT;
		case bx::FieldKind::BOOL:
			return Variant::BOOL;
		case bx::FieldKind::INT:
		case bx::FieldKind::U64:
			return Variant::INT;
		case bx::FieldKind::VEC2:
			return Variant::VECTOR2;
	}
	return Variant::NIL;
}

bool field_set(const String &name, bx::FieldSet &set) {
	if (name == "world") {
		set = bx::FieldSet::WORLD;
	} else if (name == "body") {
		set = bx::FieldSet::BODY;
	} else if (name == "shape") {
		set = bx::FieldSet::SHAPE;
	} else if (name == "joint") {
		set = bx::FieldSet::JOINT;
	} else {
		return false;
	}
	return true;
}

bool numeric(const Variant &value) {
	return value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT || value.get_type() == Variant::BOOL;
}

bool to_prop(bx::FieldSet set, const bx::FieldInfo &info, const Variant &value, bx::Prop &prop) {
	prop = bx::Prop();
	prop.id = info.id;
	switch (info.kind) {
		case bx::FieldKind::FLOAT:
		case bx::FieldKind::BOOL:
			if (!numeric(value)) {
				return false;
			}
			prop.v[0] = info.kind == bx::FieldKind::BOOL ? (bool(value) ? 1.0 : 0.0) : double(value);
			return true;
		case bx::FieldKind::INT:
			if (set == bx::FieldSet::BODY && value.get_type() == Variant::STRING) {
				// Body type by name.
				const String name = value;
				prop.v[0] = name == "static" ? 0.0 : name == "kinematic" ? 1.0 :
						name == "dynamic"                                ? 2.0 :
																		   -1.0;
				return prop.v[0] >= 0.0;
			}
			if (value.get_type() != Variant::INT) {
				return false;
			}
			prop.v[0] = double(int64_t(value));
			return true;
		case bx::FieldKind::U64:
			if (value.get_type() != Variant::INT) {
				return false;
			}
			prop.bits = uint64_t(int64_t(value));
			return true;
		case bx::FieldKind::VEC2: {
			if (value.get_type() != Variant::VECTOR2 && value.get_type() != Variant::VECTOR2I) {
				return false;
			}
			const Vector2 v = value;
			prop.v[0] = v.x;
			prop.v[1] = v.y;
			return true;
		}
	}
	return false;
}

const char *const GEOMETRY_KEYS[] = { "type", "radius", "center", "angle", "half_height", "point_a", "point_b", "half_extents", "points", "loop", "materials" };

bool geometry_key(const String &name) {
	for (const char *key : GEOMETRY_KEYS) {
		if (name == key) {
			return true;
		}
	}
	return false;
}

Error to_props(bx::FieldSet set, const Dictionary &fields, bx::Props &props, bool skip_geometry = false) {
	props.clear();
	for (const Variant &key : fields.keys()) {
		const String name = key;
		if (skip_geometry && geometry_key(name)) {
			continue;
		}
		const bx::FieldInfo *info = bx::find_field(set, name.utf8().get_data());
		ERR_FAIL_NULL_V_MSG(info, ERR_INVALID_PARAMETER, "Unknown EGPBox2DWorld field: " + name);
		bx::Prop prop;
		ERR_FAIL_COND_V_MSG(!to_prop(set, *info, fields[key], prop), ERR_INVALID_PARAMETER, "EGPBox2DWorld field " + name + " expects " + Variant::get_type_name(variant_type(info->kind)) + ".");
		props.push_back(prop);
	}
	ERR_FAIL_COND_V_MSG(!bx::validate_props(set, props), ERR_INVALID_PARAMETER, "EGPBox2DWorld fields out of range (finite values, non-negative stiffness, damping, materials and thresholds, positive lengths, revolute limits within 0.99 PI).");
	return OK;
}

Error to_geometry(const Dictionary &shape, bx::Geometry &g) {
	ERR_FAIL_COND_V_MSG(!shape.has("type"), ERR_INVALID_PARAMETER, "EGPBox2DWorld shape needs a type: circle, capsule, box, polygon, segment or chain.");
	const String type = shape["type"];
	ERR_FAIL_COND_V_MSG(!bx::shape_type_from_name(type.utf8().get_data(), g.type), ERR_INVALID_PARAMETER, "Unknown EGPBox2DWorld shape type: " + type);
	const bool rounded = g.type == bx::ShapeType::BOX || g.type == bx::ShapeType::POLYGON;
	g.radius = float(double(shape.get("radius", rounded ? 0.0 : 0.5)));
	g.center = vec(shape.get("center", Vector2()));
	g.angle = float(double(shape.get("angle", 0.0)));
	if (shape.has("half_height")) {
		const float half = float(double(shape["half_height"]));
		g.point_a = { 0.0f, -half };
		g.point_b = { 0.0f, half };
	}
	g.point_a = vec(shape.get("point_a", Vector2(g.point_a.x, g.point_a.y)));
	g.point_b = vec(shape.get("point_b", Vector2(g.point_b.x, g.point_b.y)));
	g.half_extents = vec(shape.get("half_extents", Vector2(0.5, 0.5)));
	const PackedVector2Array points = shape.get("points", PackedVector2Array());
	g.points.resize(static_cast<size_t>(points.size()));
	for (int64_t i = 0; i < points.size(); ++i) {
		g.points[size_t(i)] = vec(points[i]);
	}
	g.loop = bool(shape.get("loop", false));
	// Chains: one material Dictionary per point (the segment starting there).
	const Array materials = shape.get("materials", Array());
	for (int64_t i = 0; i < materials.size(); ++i) {
		ERR_FAIL_COND_V_MSG(materials[i].get_type() != Variant::DICTIONARY, ERR_INVALID_PARAMETER, "EGPBox2DWorld materials are Dictionaries of material fields.");
		bx::Props props;
		const Error error = to_props(bx::FieldSet::SHAPE, materials[i], props);
		if (error != OK) {
			return error;
		}
		g.materials.push_back(bx::material_from(props, b2DefaultSurfaceMaterial()));
	}
	ERR_FAIL_COND_V_MSG(!bx::valid_geometry(g), ERR_INVALID_PARAMETER, "Invalid EGPBox2DWorld " + type + " geometry.");
	return OK;
}

Error to_shape(const Dictionary &shape, bx::ShapeSpec &spec) {
	const Error error = to_geometry(shape, spec.geometry);
	if (error != OK) {
		return error;
	}
	return to_props(bx::FieldSet::SHAPE, shape, spec.props, true);
}

Variant to_variant(const bx::Value &value) {
	switch (value.kind) {
		case bx::FieldKind::FLOAT:
			return value.v[0];
		case bx::FieldKind::BOOL:
			return value.v[0] != 0.0;
		case bx::FieldKind::INT:
			return int64_t(value.v[0]);
		case bx::FieldKind::U64:
			return int64_t(value.bits);
		case bx::FieldKind::VEC2:
			return Vector2(real_t(value.v[0]), real_t(value.v[1]));
	}
	return Variant();
}

Dictionary to_dictionary(const bx::Values &values) {
	Dictionary result;
	for (const bx::Value &value : values) {
		result[String(value.name)] = to_variant(value);
	}
	return result;
}

void append_pair(PackedInt64Array &out, const bx::ShapeRef &a, const bx::ShapeRef &b) {
	out.push_back(int64_t(a.entity));
	out.push_back(int64_t(a.shape));
	out.push_back(int64_t(b.entity));
	out.push_back(int64_t(b.shape));
}

PackedInt64Array to_pairs(const std::vector<bx::StepEvents::Pair> &pairs) {
	PackedInt64Array out;
	for (const auto &pair : pairs) {
		append_pair(out, pair.a, pair.b);
	}
	return out;
}

PackedInt64Array to_refs(const std::vector<bx::ShapeRef> &refs) {
	PackedInt64Array out;
	out.resize(int64_t(refs.size()) * 2);
	for (size_t i = 0; i < refs.size(); ++i) {
		out.set(int64_t(i) * 2, int64_t(refs[i].entity));
		out.set(int64_t(i) * 2 + 1, int64_t(refs[i].shape));
	}
	return out;
}

Vector2 to_vector(b2Vec2 v) {
	return Vector2(v.x, v.y);
}
} // namespace

void EGPBox2DWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "tick_rate", "substeps", "worker_count", "gravity"), &EGPBox2DWorld::configure, DEFVAL(60), DEFVAL(4), DEFVAL(1), DEFVAL(Vector2(0, 980)));
	ClassDB::bind_method(D_METHOD("queue_create_body", "entity_id", "sequence", "body", "shapes"), &EGPBox2DWorld::queue_create_body);
	ClassDB::bind_method(D_METHOD("queue_destroy_body", "entity_id", "sequence"), &EGPBox2DWorld::queue_destroy_body);
	ClassDB::bind_method(D_METHOD("queue_set_body", "entity_id", "sequence", "fields"), &EGPBox2DWorld::queue_set_body);
	ClassDB::bind_method(D_METHOD("queue_add_shape", "entity_id", "sequence", "shape_index", "shape"), &EGPBox2DWorld::queue_add_shape);
	ClassDB::bind_method(D_METHOD("queue_set_shape", "entity_id", "sequence", "shape_index", "fields", "material_index"), &EGPBox2DWorld::queue_set_shape, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("queue_destroy_shape", "entity_id", "sequence", "shape_index"), &EGPBox2DWorld::queue_destroy_shape);
	ClassDB::bind_method(D_METHOD("queue_joint", "joint_id", "sequence", "type", "body_a", "body_b", "fields"), &EGPBox2DWorld::queue_joint, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("queue_set_joint", "joint_id", "sequence", "fields"), &EGPBox2DWorld::queue_set_joint);
	ClassDB::bind_method(D_METHOD("queue_destroy_joint", "joint_id", "sequence"), &EGPBox2DWorld::queue_destroy_joint);
	ClassDB::bind_method(D_METHOD("queue_apply", "entity_id", "sequence", "kind", "value", "scalar", "point"), &EGPBox2DWorld::queue_apply, DEFVAL(0.0), DEFVAL(Vector2()));
	ClassDB::bind_method(D_METHOD("queue_apply_wind", "entity_id", "sequence", "shape_index", "wind", "drag", "lift"), &EGPBox2DWorld::queue_apply_wind);
	ClassDB::bind_method(D_METHOD("queue_set_world", "sequence", "fields"), &EGPBox2DWorld::queue_set_world);
	ClassDB::bind_method(D_METHOD("queue_explode", "sequence", "position", "radius", "falloff", "impulse_per_length", "mask"), &EGPBox2DWorld::queue_explode, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("apply_queued_commands"), &EGPBox2DWorld::apply_queued_commands);
	ClassDB::bind_method(D_METHOD("clear_pending_commands"), &EGPBox2DWorld::clear_pending_commands);
	ClassDB::bind_method(D_METHOD("step_tick", "expected_tick"), &EGPBox2DWorld::step_tick);
	ClassDB::bind_method(D_METHOD("get_tick"), &EGPBox2DWorld::get_tick);
	ClassDB::bind_method(D_METHOD("get_body_count"), &EGPBox2DWorld::get_body_count);
	ClassDB::bind_method(D_METHOD("get_joint_count"), &EGPBox2DWorld::get_joint_count);
	ClassDB::bind_method(D_METHOD("get_body_states", "entity_ids"), &EGPBox2DWorld::get_body_states);
	ClassDB::bind_method(D_METHOD("get_world"), &EGPBox2DWorld::get_world);
	ClassDB::bind_method(D_METHOD("get_body", "entity_id"), &EGPBox2DWorld::get_body);
	ClassDB::bind_method(D_METHOD("get_shape", "entity_id", "shape_index"), &EGPBox2DWorld::get_shape);
	ClassDB::bind_method(D_METHOD("get_joint", "joint_id"), &EGPBox2DWorld::get_joint);
	ClassDB::bind_method(D_METHOD("get_entities"), &EGPBox2DWorld::get_entities);
	ClassDB::bind_method(D_METHOD("get_joint_ids"), &EGPBox2DWorld::get_joint_ids);
	ClassDB::bind_method(D_METHOD("get_events"), &EGPBox2DWorld::get_events);
	ClassDB::bind_method(D_METHOD("cast_rays", "origins", "translations", "mask"), &EGPBox2DWorld::cast_rays, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("overlap_aabb", "lower", "upper", "mask"), &EGPBox2DWorld::overlap_aabb, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("overlap_shape", "shape", "position", "angle", "mask"), &EGPBox2DWorld::overlap_shape, DEFVAL(0.0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("cast_shape", "shape", "position", "angle", "translation", "mask", "ignore_entity"), &EGPBox2DWorld::cast_shape, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("move_capsule", "position", "point_a", "point_b", "radius", "translation", "mask"), &EGPBox2DWorld::move_capsule, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("capture_snapshot"), &EGPBox2DWorld::capture_snapshot);
	ClassDB::bind_method(D_METHOD("restore_snapshot", "bytes"), &EGPBox2DWorld::restore_snapshot);
	ClassDB::bind_method(D_METHOD("get_state_hash"), &EGPBox2DWorld::get_state_hash);
	ClassDB::bind_method(D_METHOD("get_simulation_fingerprint"), &EGPBox2DWorld::get_simulation_fingerprint);
	ClassDB::bind_static_method("EGPBox2DWorld", D_METHOD("get_field_names", "set"), &EGPBox2DWorld::get_field_names);
	BIND_ENUM_CONSTANT(APPLY_FORCE);
	BIND_ENUM_CONSTANT(APPLY_FORCE_AT_POINT);
	BIND_ENUM_CONSTANT(APPLY_TORQUE);
	BIND_ENUM_CONSTANT(APPLY_IMPULSE);
	BIND_ENUM_CONSTANT(APPLY_IMPULSE_AT_POINT);
	BIND_ENUM_CONSTANT(APPLY_ANGULAR_IMPULSE);
}

Error EGPBox2DWorld::configure(int64_t rate, int64_t steps, int64_t worker_count, const Vector2 &gravity) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(rate < 1 || rate > 240 || steps < 1 || steps > 16 || worker_count < 1 || worker_count > 64, ERR_INVALID_PARAMETER);
	return to_error(simulation.configure(uint32_t(rate), uint32_t(steps), uint32_t(worker_count), vec(gravity), enqueue_task_callback, finish_task_callback));
}

Error EGPBox2DWorld::queue_command(const bx::Command &c) {
	return to_error(simulation.queue(c));
}

Error EGPBox2DWorld::queue_create_body(int64_t entity, int64_t sequence, const Dictionary &body, const TypedArray<Dictionary> &shape_list) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || shape_list.size() > int64_t(bx::DeterministicWorld2D::MAX_SHAPES_PER_BODY), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::CREATE_BODY;
	Error error = to_props(bx::FieldSet::BODY, body, c.props);
	if (error != OK) {
		return error;
	}
	c.shapes.resize(static_cast<size_t>(shape_list.size()));
	for (int64_t i = 0; i < shape_list.size(); ++i) {
		error = to_shape(shape_list[i], c.shapes[size_t(i)]);
		if (error != OK) {
			return error;
		}
	}
	return queue_command(c);
}

Error EGPBox2DWorld::queue_destroy_body(int64_t entity, int64_t sequence) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::DESTROY_BODY;
	return queue_command(c);
}

Error EGPBox2DWorld::queue_set_body(int64_t entity, int64_t sequence, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::SET_BODY;
	const Error error = to_props(bx::FieldSet::BODY, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox2DWorld::queue_add_shape(int64_t entity, int64_t sequence, int64_t index, const Dictionary &shape) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index >= int64_t(bx::DeterministicWorld2D::MAX_SHAPES_PER_BODY), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::ADD_SHAPE;
	c.shape_index = uint32_t(index);
	c.shapes.resize(1);
	const Error error = to_shape(shape, c.shapes[0]);
	return error != OK ? error : queue_command(c);
}

Error EGPBox2DWorld::queue_set_shape(int64_t entity, int64_t sequence, int64_t index, const Dictionary &fields, int64_t material_index) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index > int64_t(UINT32_MAX), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::SET_SHAPE;
	c.shape_index = uint32_t(index);
	ERR_FAIL_COND_V(material_index < -1 || material_index > 65535, ERR_INVALID_PARAMETER);
	c.material_index = int32_t(material_index);
	const Error error = to_props(bx::FieldSet::SHAPE, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox2DWorld::queue_destroy_shape(int64_t entity, int64_t sequence, int64_t index) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index > int64_t(UINT32_MAX), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::DESTROY_SHAPE;
	c.shape_index = uint32_t(index);
	return queue_command(c);
}

Error EGPBox2DWorld::queue_joint(int64_t joint, int64_t sequence, const String &type, int64_t body_a, int64_t body_b, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence) || body_a <= 0 || body_b <= 0 || body_a == body_b, ERR_INVALID_PARAMETER);
	bx::Command c;
	ERR_FAIL_COND_V_MSG(!bx::joint_type_from_name(type.utf8().get_data(), c.joint_type), ERR_INVALID_PARAMETER, "Unknown EGPBox2DWorld joint type: " + type);
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::CREATE_JOINT;
	c.body_a = uint64_t(body_a);
	c.body_b = uint64_t(body_b);
	const Error error = to_props(bx::FieldSet::JOINT, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox2DWorld::queue_set_joint(int64_t joint, int64_t sequence, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::SET_JOINT;
	const Error error = to_props(bx::FieldSet::JOINT, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox2DWorld::queue_destroy_joint(int64_t joint, int64_t sequence) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::DESTROY_JOINT;
	return queue_command(c);
}

Error EGPBox2DWorld::queue_apply(int64_t entity, int64_t sequence, ApplyKind kind, const Vector2 &value, double scalar, const Vector2 &point) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || kind < APPLY_FORCE || kind > APPLY_ANGULAR_IMPULSE, ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::APPLY;
	c.apply = bx::ApplyKind(kind);
	c.value = vec(value);
	c.scalar = float(scalar);
	c.point = vec(point);
	return queue_command(c);
}

Error EGPBox2DWorld::queue_apply_wind(int64_t entity, int64_t sequence, int64_t index, const Vector2 &wind, double drag, double lift) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index >= int64_t(bx::DeterministicWorld2D::MAX_SHAPES_PER_BODY), ERR_INVALID_PARAMETER);
	bx::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = bx::Operation::APPLY;
	c.apply = bx::ApplyKind::WIND;
	c.shape_index = uint32_t(index);
	c.value = vec(wind);
	c.drag = float(drag);
	c.lift = float(lift);
	return queue_command(c);
}

Error EGPBox2DWorld::queue_world_command(int64_t sequence, bx::Command &c) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(sequence < 0 || uint64_t(sequence) > UINT32_MAX, ERR_INVALID_PARAMETER);
	c.entity = bx::DeterministicWorld2D::WORLD_KEY;
	c.sequence = uint32_t(sequence);
	return queue_command(c);
}

Error EGPBox2DWorld::queue_set_world(int64_t sequence, const Dictionary &fields) {
	bx::Command c;
	c.operation = bx::Operation::SET_WORLD;
	const Error error = to_props(bx::FieldSet::WORLD, fields, c.props);
	return error != OK ? error : queue_world_command(sequence, c);
}

Error EGPBox2DWorld::queue_explode(int64_t sequence, const Vector2 &position, double radius, double falloff, double impulse_per_length, int64_t mask) {
	bx::Command c;
	c.operation = bx::Operation::EXPLODE;
	c.value = vec(position);
	c.radius = float(radius);
	c.falloff = float(falloff);
	c.impulse_per_length = float(impulse_per_length);
	c.mask = uint64_t(mask);
	return queue_world_command(sequence, c);
}

Error EGPBox2DWorld::apply_queued_commands() {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	return to_error(simulation.apply_queued_commands());
}

void EGPBox2DWorld::clear_pending_commands() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	simulation.clear_pending_commands();
}

Error EGPBox2DWorld::step_tick(int64_t expected) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(expected < 1, ERR_INVALID_PARAMETER);
	return to_error(simulation.step_tick(uint64_t(expected)));
}

int64_t EGPBox2DWorld::get_tick() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_tick());
}

int64_t EGPBox2DWorld::get_body_count() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_body_count());
}

int64_t EGPBox2DWorld::get_joint_count() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_joint_count());
}

PackedFloat32Array EGPBox2DWorld::get_body_states(const PackedInt64Array &entities) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedFloat32Array());
	PackedFloat32Array result;
	ERR_FAIL_COND_V(result.resize(int64_t(entities.size()) * BODY_RECORD) != OK, PackedFloat32Array());
	float *out = result.ptrw();
	for (int64_t i = 0; i < entities.size(); ++i) {
		b2Vec2 position, linear;
		float angle = 0.0f, angular = 0.0f;
		float *record = out + i * BODY_RECORD;
		if (entities[i] <= 0 || !simulation.get_body_state(uint64_t(entities[i]), position, angle, linear, angular)) {
			// Unknown bodies read as NaN so callers can detect them without a second call.
			for (int f = 0; f < BODY_RECORD; ++f) {
				record[f] = NAN;
			}
			continue;
		}
		const float values[BODY_RECORD] = { position.x, position.y, angle, linear.x, linear.y, angular };
		std::memcpy(record, values, sizeof(values));
	}
	return result;
}

Dictionary EGPBox2DWorld::get_world() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (!simulation.read_world(values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	result["tick"] = int64_t(simulation.get_tick());
	return result;
}

Dictionary EGPBox2DWorld::get_body(int64_t entity) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (entity <= 0 || !simulation.read_body(uint64_t(entity), values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	PackedInt32Array indices;
	for (const uint32_t index : simulation.get_shape_indices(uint64_t(entity))) {
		indices.push_back(int32_t(index));
	}
	result["shapes"] = indices;
	return result;
}

Dictionary EGPBox2DWorld::get_shape(int64_t entity, int64_t index) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (entity <= 0 || index < 0 || index > int64_t(UINT32_MAX) || !simulation.read_shape(uint64_t(entity), uint32_t(index), values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	std::vector<bx::Values> materials;
	if (simulation.read_shape_materials(uint64_t(entity), uint32_t(index), materials) && materials.size() > 1) {
		Array table;
		for (const bx::Values &material : materials) {
			table.push_back(to_dictionary(material));
		}
		result["materials"] = table;
	}
	return result;
}

Dictionary EGPBox2DWorld::get_joint(int64_t joint) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	bx::JointType type;
	uint64_t body_a = 0, body_b = 0;
	if (joint <= 0 || !simulation.read_joint(uint64_t(joint), values, type, body_a, body_b)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	result["type"] = String(bx::joint_type_name(type));
	result["body_a"] = int64_t(body_a);
	result["body_b"] = int64_t(body_b);
	return result;
}

PackedInt64Array EGPBox2DWorld::get_entities() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	for (const uint64_t entity : simulation.get_entities()) {
		result.push_back(int64_t(entity));
	}
	return result;
}

PackedInt64Array EGPBox2DWorld::get_joint_ids() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	for (const uint64_t joint : simulation.get_joint_ids()) {
		result.push_back(int64_t(joint));
	}
	return result;
}

Dictionary EGPBox2DWorld::get_events() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const bx::StepEvents &events = simulation.get_events();
	Dictionary result;
	result["contact_begin"] = to_pairs(events.contact_begin);
	result["contact_end"] = to_pairs(events.contact_end);
	result["sensor_begin"] = to_pairs(events.sensor_begin);
	result["sensor_end"] = to_pairs(events.sensor_end);
	PackedInt64Array hits;
	PackedVector2Array points, normals;
	PackedFloat32Array speeds;
	for (const auto &hit : events.contact_hit) {
		append_pair(hits, hit.a, hit.b);
		points.push_back(to_vector(hit.point));
		normals.push_back(to_vector(hit.normal));
		speeds.push_back(hit.approach_speed);
	}
	result["contact_hit"] = hits;
	result["contact_hit_point"] = points;
	result["contact_hit_normal"] = normals;
	result["contact_hit_speed"] = speeds;
	PackedInt64Array moved, asleep;
	PackedFloat32Array transforms;
	moved.resize(int64_t(events.moved.size()));
	transforms.resize(int64_t(events.moved.size()) * 3);
	float *out = transforms.ptrw();
	for (size_t i = 0; i < events.moved.size(); ++i) {
		const auto &move = events.moved[i];
		moved.set(int64_t(i), int64_t(move.entity));
		out[i * 3] = move.position.x;
		out[i * 3 + 1] = move.position.y;
		out[i * 3 + 2] = move.angle;
		if (move.fell_asleep) {
			asleep.push_back(int64_t(move.entity));
		}
	}
	result["moved"] = moved;
	result["moved_transforms"] = transforms;
	result["fell_asleep"] = asleep;
	PackedInt64Array joints;
	for (const uint64_t joint : events.joints) {
		joints.push_back(int64_t(joint));
	}
	result["joint_threshold"] = joints;
	return result;
}

Dictionary EGPBox2DWorld::cast_rays(const PackedVector2Array &origins, const PackedVector2Array &translations, int64_t mask) const {
	Dictionary result;
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, result);
	ERR_FAIL_COND_V(origins.size() != translations.size(), result);
	const int64_t count = origins.size();
	std::vector<b2Vec2> from(static_cast<size_t>(count)), along(static_cast<size_t>(count));
	for (int64_t i = 0; i < count; ++i) {
		from[size_t(i)] = vec(origins[i]);
		along[size_t(i)] = vec(translations[i]);
	}
	std::vector<bx::DeterministicWorld2D::RayHit> hits(static_cast<size_t>(count));
	simulation.cast_rays(from.data(), along.data(), static_cast<size_t>(count), hits.data(), uint64_t(mask));
	PackedByteArray hit;
	PackedFloat32Array fraction;
	PackedVector2Array point, normal;
	PackedInt64Array entity, shape;
	hit.resize(count);
	fraction.resize(count);
	point.resize(count);
	normal.resize(count);
	entity.resize(count);
	shape.resize(count);
	for (int64_t i = 0; i < count; ++i) {
		const auto &h = hits[size_t(i)];
		hit.set(i, h.hit ? 1 : 0);
		fraction.set(i, h.fraction);
		point.set(i, to_vector(h.point));
		normal.set(i, to_vector(h.normal));
		entity.set(i, int64_t(h.entity));
		shape.set(i, int64_t(h.shape));
	}
	result["hit"] = hit;
	result["fraction"] = fraction;
	result["point"] = point;
	result["normal"] = normal;
	result["entity"] = entity;
	result["shape"] = shape;
	return result;
}

PackedInt64Array EGPBox2DWorld::overlap_aabb(const Vector2 &lower, const Vector2 &upper, int64_t mask) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	std::vector<bx::ShapeRef> found;
	simulation.overlap_aabb(vec(lower), vec(upper), uint64_t(mask), found);
	return to_refs(found);
}

PackedInt64Array EGPBox2DWorld::overlap_shape(const Dictionary &shape, const Vector2 &position, double angle, int64_t mask) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	bx::Geometry geometry;
	ERR_FAIL_COND_V(to_geometry(shape, geometry) != OK, PackedInt64Array());
	std::vector<bx::ShapeRef> found;
	ERR_FAIL_COND_V_MSG(!simulation.overlap_shape(geometry, vec(position), float(angle), uint64_t(mask), found), PackedInt64Array(), "overlap_shape takes circle, capsule, box, polygon or segment geometry.");
	return to_refs(found);
}

Dictionary EGPBox2DWorld::cast_shape(const Dictionary &shape, const Vector2 &position, double angle, const Vector2 &translation, int64_t mask, int64_t ignore) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	ERR_FAIL_COND_V(ignore < 0, Dictionary());
	bx::Geometry geometry;
	ERR_FAIL_COND_V(to_geometry(shape, geometry) != OK, Dictionary());
	bx::DeterministicWorld2D::RayHit hit;
	ERR_FAIL_COND_V_MSG(!simulation.cast_shape(geometry, vec(position), float(angle), vec(translation), uint64_t(mask), uint64_t(ignore), hit), Dictionary(), "cast_shape takes circle, capsule, box, polygon or segment geometry.");
	Dictionary result;
	result["hit"] = hit.hit;
	result["fraction"] = hit.fraction;
	result["point"] = to_vector(hit.point);
	result["normal"] = to_vector(hit.normal);
	result["entity"] = int64_t(hit.entity);
	result["shape"] = int64_t(hit.shape);
	return result;
}

Dictionary EGPBox2DWorld::move_capsule(const Vector2 &position, const Vector2 &point_a, const Vector2 &point_b, double radius, const Vector2 &translation, int64_t mask) const {
	Dictionary result;
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, result);
	ERR_FAIL_COND_V(!(radius > 0.0), result);
	b2Vec2 clipped;
	const b2Vec2 moved = simulation.move_capsule(vec(position), vec(point_a), vec(point_b), float(radius), vec(translation), uint64_t(mask), &clipped);
	result["position"] = to_vector(moved);
	result["clipped"] = to_vector(clipped);
	return result;
}

PackedByteArray EGPBox2DWorld::capture_snapshot() {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedByteArray());
	std::vector<uint8_t> bytes;
	ERR_FAIL_COND_V(simulation.capture_snapshot(bytes) != bx::Result::OK, PackedByteArray());
	PackedByteArray result;
	result.resize(int64_t(bytes.size()));
	std::memcpy(result.ptrw(), bytes.data(), bytes.size());
	return result;
}

Error EGPBox2DWorld::restore_snapshot(const PackedByteArray &bytes) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(bytes.size() < 128 || bytes.size() > int64_t(bx::DeterministicWorld2D::MAX_SNAPSHOT_BYTES), ERR_FILE_CORRUPT);
	const std::vector<uint8_t> copy(bytes.ptr(), bytes.ptr() + bytes.size());
	return to_error(simulation.restore_snapshot(copy));
}

String EGPBox2DWorld::get_state_hash() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, String());
	char text[17];
	std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(simulation.get_state_hash()));
	return String(text);
}

String EGPBox2DWorld::get_simulation_fingerprint() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, String());
	return String(simulation.get_simulation_fingerprint().c_str());
}

Dictionary EGPBox2DWorld::get_field_names(const String &set_name) {
	bx::FieldSet set;
	ERR_FAIL_COND_V_MSG(!field_set(set_name, set), Dictionary(), "Field sets are world, body, shape and joint.");
	Dictionary result;
	for (const bx::FieldInfo &info : bx::all_fields(set)) {
		result[String(info.name)] = int64_t(variant_type(info.kind));
	}
	return result;
}
