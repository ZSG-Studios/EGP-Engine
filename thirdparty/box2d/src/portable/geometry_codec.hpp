// SPDX-License-Identifier: MIT
#pragma once
#include "cold_body_codec.hpp"
namespace superpos::box2d_portable {
constexpr uint32_t circle_geometry_kind=6657;
struct CircleGeometryImage {std::array<canonical::Atom,3> atoms{};std::array<canonical::Field,3> fields{};canonical::Record record{};CircleGeometryImage()noexcept;CircleGeometryImage(const CircleGeometryImage &)=delete;CircleGeometryImage &operator=(const CircleGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> circle_geometry_fields()noexcept;
Status capture_circle_geometry(const b2Circle &,canonical::Identity,CircleGeometryImage &)noexcept;
Status restore_circle_geometry(const canonical::Record &,b2Circle &)noexcept;
constexpr uint32_t capsule_geometry_kind=6658;
struct CapsuleGeometryImage {std::array<canonical::Atom,5> atoms{};std::array<canonical::Field,5> fields{};canonical::Record record{};CapsuleGeometryImage()noexcept;CapsuleGeometryImage(const CapsuleGeometryImage &)=delete;CapsuleGeometryImage &operator=(const CapsuleGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> capsule_geometry_fields()noexcept;
Status capture_capsule_geometry(const b2Capsule &,canonical::Identity,CapsuleGeometryImage &)noexcept;
Status restore_capsule_geometry(const canonical::Record &,b2Capsule &)noexcept;
constexpr uint32_t segment_geometry_kind=6659;
struct SegmentGeometryImage {std::array<canonical::Atom,4> atoms{};std::array<canonical::Field,4> fields{};canonical::Record record{};SegmentGeometryImage()noexcept;SegmentGeometryImage(const SegmentGeometryImage &)=delete;SegmentGeometryImage &operator=(const SegmentGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> segment_geometry_fields()noexcept;
Status capture_segment_geometry(const b2Segment &,canonical::Identity,SegmentGeometryImage &)noexcept;
Status restore_segment_geometry(const canonical::Record &,b2Segment &)noexcept;
constexpr uint32_t chain_segment_geometry_kind=6661;
struct ChainSegmentGeometryImage {std::array<canonical::Atom,9> atoms{};std::array<canonical::Field,9> fields{};canonical::Record record{};ChainSegmentGeometryImage()noexcept;ChainSegmentGeometryImage(const ChainSegmentGeometryImage &)=delete;ChainSegmentGeometryImage &operator=(const ChainSegmentGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> chain_segment_geometry_fields()noexcept;
Status capture_chain_segment_geometry(const b2ChainSegment &,canonical::Identity,const canonical::IdentityMap &,ChainSegmentGeometryImage &)noexcept;
Status restore_chain_segment_geometry(const canonical::Record &,const canonical::IdentityMap &,b2ChainSegment &)noexcept;
constexpr uint32_t filter_geometry_kind=6662;
struct FilterGeometryImage {std::array<canonical::Atom,3> atoms{};std::array<canonical::Field,3> fields{};canonical::Record record{};FilterGeometryImage()noexcept;FilterGeometryImage(const FilterGeometryImage &)=delete;FilterGeometryImage &operator=(const FilterGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> filter_geometry_fields()noexcept;
Status capture_filter_geometry(const b2Filter &,canonical::Identity,FilterGeometryImage &)noexcept;
Status restore_filter_geometry(const canonical::Record &,b2Filter &)noexcept;
constexpr uint32_t surface_material_geometry_kind=6663;
struct SurfaceMaterialGeometryImage {std::array<canonical::Atom,6> atoms{};std::array<canonical::Field,6> fields{};canonical::Record record{};SurfaceMaterialGeometryImage()noexcept;SurfaceMaterialGeometryImage(const SurfaceMaterialGeometryImage &)=delete;SurfaceMaterialGeometryImage &operator=(const SurfaceMaterialGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> surface_material_geometry_fields()noexcept;
Status capture_surface_material_geometry(const b2SurfaceMaterial &,canonical::Identity,SurfaceMaterialGeometryImage &)noexcept;
Status restore_surface_material_geometry(const canonical::Record &,b2SurfaceMaterial &)noexcept;
constexpr uint32_t aabb_geometry_kind=6664;
struct AABBGeometryImage {std::array<canonical::Atom,4> atoms{};std::array<canonical::Field,4> fields{};canonical::Record record{};AABBGeometryImage()noexcept;AABBGeometryImage(const AABBGeometryImage &)=delete;AABBGeometryImage &operator=(const AABBGeometryImage &)=delete;};
std::span<const canonical::FieldSpec> aabb_geometry_fields()noexcept;
Status capture_aabb_geometry(const b2AABB &,canonical::Identity,AABBGeometryImage &)noexcept;
Status restore_aabb_geometry(const canonical::Record &,b2AABB &)noexcept;
}
