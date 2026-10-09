// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "box3d_area_impl_3d.hpp"

#include "../misc/type_conversions.hpp"
#include "precompiled.hpp"

#include <box3d/box3d.h>

b3BodyId Box3DAreaImpl3D::_create_body_id(b3WorldId p_world_id) {
	b3BodyDef def = b3DefaultBodyDef();
	const b3Transform t = godot_to_b3_transform(get_transform());
	def.type = b3_kinematicBody;
	def.position = t.p;
	def.rotation = t.q;
	def.userData = this;
	def.isAwake = true;
	def.isEnabled = true;
	return b3CreateBody(p_world_id, &def);
}

Variant Box3DAreaImpl3D::get_param(PS3DE::AreaParameter p_param) const {
	switch (p_param) {
		case PS3DE::AREA_PARAM_GRAVITY_OVERRIDE_MODE:
			return gravity_mode;
		case PS3DE::AREA_PARAM_GRAVITY:
			return gravity;
		case PS3DE::AREA_PARAM_GRAVITY_VECTOR:
			return gravity_vector;
		case PS3DE::AREA_PARAM_GRAVITY_IS_POINT:
			return point_gravity;
		case PS3DE::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE:
			return point_gravity_distance;
		case PS3DE::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE:
			return linear_damp_mode;
		case PS3DE::AREA_PARAM_LINEAR_DAMP:
			return linear_damp;
		case PS3DE::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE:
			return angular_damp_mode;
		case PS3DE::AREA_PARAM_ANGULAR_DAMP:
			return angular_damp;
		case PS3DE::AREA_PARAM_PRIORITY:
			return priority;
		case PS3DE::AREA_PARAM_WIND_FORCE_MAGNITUDE:
			return wind_force;
		case PS3DE::AREA_PARAM_WIND_SOURCE:
			return wind_source;
		case PS3DE::AREA_PARAM_WIND_DIRECTION:
			return wind_direction;
		case PS3DE::AREA_PARAM_WIND_ATTENUATION_FACTOR:
			return wind_attenuation;
		default:
			return Variant();
	}
}

void Box3DAreaImpl3D::set_param(PS3DE::AreaParameter p_param, const Variant &p_value) {
	switch (p_param) {
		case PS3DE::AREA_PARAM_GRAVITY_OVERRIDE_MODE:
			gravity_mode = (OverrideMode)(int)p_value;
			break;
		case PS3DE::AREA_PARAM_GRAVITY:
			gravity = p_value;
			break;
		case PS3DE::AREA_PARAM_GRAVITY_VECTOR:
			gravity_vector = p_value;
			break;
		case PS3DE::AREA_PARAM_GRAVITY_IS_POINT:
			point_gravity = p_value;
			break;
		case PS3DE::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE:
			point_gravity_distance = p_value;
			break;
		case PS3DE::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE:
			linear_damp_mode = (OverrideMode)(int)p_value;
			break;
		case PS3DE::AREA_PARAM_LINEAR_DAMP:
			linear_damp = p_value;
			break;
		case PS3DE::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE:
			angular_damp_mode = (OverrideMode)(int)p_value;
			break;
		case PS3DE::AREA_PARAM_ANGULAR_DAMP:
			angular_damp = p_value;
			break;
		case PS3DE::AREA_PARAM_PRIORITY:
			priority = p_value;
			break;
		case PS3DE::AREA_PARAM_WIND_FORCE_MAGNITUDE: {
			real_t value = p_value;
			ERR_FAIL_COND(!std::isfinite(value));
			wind_force = value;
			break;
		}
		case PS3DE::AREA_PARAM_WIND_SOURCE:
			wind_source = p_value;
			break;
		case PS3DE::AREA_PARAM_WIND_DIRECTION:
			wind_direction = p_value;
			break;
		case PS3DE::AREA_PARAM_WIND_ATTENUATION_FACTOR: {
			real_t value = p_value;
			ERR_FAIL_COND(!std::isfinite(value) || value < 0);
			wind_attenuation = value;
			break;
		}
		default:
			break;
	}
}

Vector3 Box3DAreaImpl3D::compute_gravity(const Vector3 &p_position) const {
	if (!point_gravity) {
		// Godot does not normalize the direction, so its length scales the strength.
		return gravity_vector * gravity;
	}

	// When gravity is a point, Godot carries the center in gravity_vector, in area space.
	const Vector3 to_center = get_transform().xform(gravity_vector) - p_position;
	if (point_gravity_distance <= 0.0f) {
		return to_center.normalized() * gravity;
	}
	const real_t distance_squared = to_center.length_squared();
	if (distance_squared <= 0.0) {
		return Vector3();
	}
	const real_t strength = gravity * point_gravity_distance * point_gravity_distance / distance_squared;
	return to_center.normalized() * strength;
}

Vector3 Box3DAreaImpl3D::compute_wind(const Vector3 &p_position, const Vector3 &p_normal, real_t p_area) const {
	if (wind_force == 0 || !wind_direction.is_finite() || !wind_source.is_finite()) {
		return Vector3();
	}
	const real_t distance = MAX((p_position - wind_source).dot(wind_direction), real_t(0.01));
	const real_t attenuation = wind_attenuation == 0 ? real_t(1) : Math::pow(distance, -wind_attenuation);
	const Vector3 force = p_normal * (wind_force * p_area * p_normal.dot(wind_direction) * attenuation);
	return force.is_finite() ? force : Vector3();
}

bool Box3DAreaImpl3D::add_overlap(Box3DShapedObjectImpl3D *p_other) {
	int32_t *count = overlaps.getptr(p_other);
	if (count == nullptr) {
		overlaps.insert(p_other, 1);
		return true;
	}
	(*count)++;
	return false;
}

bool Box3DAreaImpl3D::remove_overlap(Box3DShapedObjectImpl3D *p_other) {
	int32_t *count = overlaps.getptr(p_other);
	if (count == nullptr) {
		return false;
	}
	(*count)--;
	if (*count <= 0) {
		overlaps.erase(p_other);
		return true;
	}
	return false;
}
