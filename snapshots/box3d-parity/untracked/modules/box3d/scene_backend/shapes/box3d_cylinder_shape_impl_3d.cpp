// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "box3d_cylinder_shape_impl_3d.hpp"

#include "precompiled.hpp"

Variant Box3DCylinderShapeImpl3D::get_data() const {
	Dictionary data;
	data["radius"] = radius;
	data["height"] = height;
	return data;
}

void Box3DCylinderShapeImpl3D::set_data(const Variant &p_data) {
	ERR_FAIL_COND(p_data.get_type() != Variant::DICTIONARY);
	const Dictionary data = p_data;
	ERR_FAIL_COND(!data.has("radius"));
	ERR_FAIL_COND(!data.has("height"));
	radius = data["radius"];
	height = data["height"];
}

AABB Box3DCylinderShapeImpl3D::get_aabb() const {
	const Vector3 half(radius, height * 0.5, radius);
	return AABB(-half, half * 2.0);
}
