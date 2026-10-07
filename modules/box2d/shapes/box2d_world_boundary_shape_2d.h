/**************************************************************************/
/*  box2d_world_boundary_shape_2d.h                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "box2d_shape_2d.h"

// Resource metadata remains valid for editor inspection, serialization and RID
// ownership. Box2D has no infinite half-space primitive; do not approximate one
// with a finite edge or silently publish an empty native collision shape.
class Box2DWorldBoundaryShape2D : public Box2DShape2D {
public:
	void add_to_body(Box2DShapeInstance *) const override {
		ERR_PRINT_ONCE("WorldBoundaryShape2D collision is not supported by Box2D. Use finite SegmentShape2D/RectangleShape2D boundaries; no collision fixture was created.");
	}

	int cast(const CastQuery &, const Transform2D &, LocalVector<CastHit> &) const override {
		ERR_PRINT_ONCE("WorldBoundaryShape2D cast queries are not supported by Box2D. Use a supported finite query shape.");
		return 0;
	}

	int overlap(const OverlapQuery &, const Transform2D &, LocalVector<ShapeOverlap> &) const override {
		ERR_PRINT_ONCE("WorldBoundaryShape2D overlap queries are not supported by Box2D. Use a supported finite query shape.");
		return 0;
	}

	PS2DE::ShapeType get_type() const override { return PS2DE::ShapeType::SHAPE_WORLD_BOUNDARY; }
};
