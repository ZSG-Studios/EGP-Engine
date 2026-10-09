// SPDX-License-Identifier: MIT
#pragma once
#include "polygon_codec.hpp"
#include "sensor_codec.hpp"
extern "C" {
#include "shape.h"
}
namespace superpos::box2d_portable {
constexpr uint32_t shape_proxy_kind=0x1B00;
struct ShapeMappings {const canonical::IdentityMap &shape,&body,&sensor,&chain;const canonical::PointerIdentityMap &binding;std::array<const canonical::IdentityMap *,3> proxies;};
struct ShapeImage {std::array<canonical::Atom,42> atoms{};std::array<canonical::Field,42> fields{};canonical::Record base{};std::array<canonical::Record,2> records{};
 CircleGeometryImage circle;CapsuleGeometryImage capsule;SegmentGeometryImage segment;PolygonGeometryImage polygon;ChainSegmentGeometryImage chain_segment;
 ShapeImage()noexcept;ShapeImage(const ShapeImage &)=delete;ShapeImage &operator=(const ShapeImage &)=delete;};
std::span<const canonical::FieldSpec> shape_fields()noexcept;
Status capture_shape(const b2Shape &,canonical::Identity,const ShapeMappings &,ShapeImage &)noexcept;
Status restore_shape(const canonical::Record &,const canonical::Record &,const ShapeMappings &,b2Shape &)noexcept;
}
