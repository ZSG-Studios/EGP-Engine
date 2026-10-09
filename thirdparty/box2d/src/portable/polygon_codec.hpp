// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_codec.hpp"
namespace superpos::box2d_portable {
constexpr uint32_t polygon_geometry_kind=0x1A04;
static_assert(B2_MAX_POLYGON_VERTICES==8,"Unqualified polygon vertex profile");
struct PolygonGeometryImage {std::array<canonical::Atom,36> atoms{};std::array<canonical::Field,5> fields{};canonical::Record record{};
 PolygonGeometryImage()noexcept;PolygonGeometryImage(const PolygonGeometryImage &)=delete;PolygonGeometryImage &operator=(const PolygonGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> polygon_geometry_fields()noexcept;
Status capture_polygon_geometry(const b2Polygon &,canonical::Identity,PolygonGeometryImage &)noexcept;
Status restore_polygon_geometry(const canonical::Record &,b2Polygon &)noexcept;
}
