// SPDX-License-Identifier: MIT
#include "geometry_codec.hpp"
#include <bit>
#include <cmath>
#include <climits>
namespace superpos::box2d_portable {namespace {using namespace canonical;
Status validate_geometry(const Record &r,uint32_t kind,std::span<const FieldSpec> defs)noexcept{
 if(r.identity.kind!=kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=defs.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<defs.size();++i){const auto &f=r.fields[i];const auto &d=defs[i];if(f.id!=d.id||f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){if(a.bits||a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);}
 else if(a.identity.kind||a.identity.simulation||a.identity.generation)return fail(Error::InvalidArgument);
 else if(d.type==AtomType::Float32&&(a.bits>UINT32_MAX||!std::isfinite(std::bit_cast<float>(uint32_t(a.bits)))))return fail(Error::NonCanonical);}
 return {};
}
}
namespace {constexpr std::array<FieldSpec,3> circle_defs{{
 {3387240348u,AtomType::Float32,0,1,1,false}, // center.y
 {3669213505u,AtomType::Float32,0,1,1,false}, // radius
 {4223926048u,AtomType::Float32,0,1,1,false}, // center.x
}};}
CircleGeometryImage::CircleGeometryImage()noexcept{for(size_t i=0;i<3;++i)fields[i]={circle_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> circle_geometry_fields()noexcept{return circle_defs;}
Status restore_circle_geometry(const Record &r,b2Circle &out)noexcept{if(auto s=validate_geometry(r,circle_geometry_kind,circle_defs);!s)return s;b2Circle staged{};
 staged.center.y=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 staged.radius=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 if(staged.radius<0)return fail(Error::NonCanonical);
 staged.center.x=std::bit_cast<float>(uint32_t(r.fields[2].atoms[0].bits));
 out=staged;return {}; }
Status capture_circle_geometry(const b2Circle &native,Identity id,CircleGeometryImage &out)noexcept{CircleGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.center.y);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.radius);
 staged.atoms[2].bits=std::bit_cast<uint32_t>(native.center.x);
 b2Circle check{};if(auto s=restore_circle_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,5> capsule_defs{{
 {191433725u,AtomType::Float32,0,1,1,false}, // center1.x
 {1322208868u,AtomType::Float32,0,1,1,false}, // center2.y
 {2650214298u,AtomType::Float32,0,1,1,false}, // radius
 {2771356093u,AtomType::Float32,0,1,1,false}, // center1.y
 {3549443520u,AtomType::Float32,0,1,1,false}, // center2.x
}};}
CapsuleGeometryImage::CapsuleGeometryImage()noexcept{for(size_t i=0;i<5;++i)fields[i]={capsule_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> capsule_geometry_fields()noexcept{return capsule_defs;}
Status restore_capsule_geometry(const Record &r,b2Capsule &out)noexcept{if(auto s=validate_geometry(r,capsule_geometry_kind,capsule_defs);!s)return s;b2Capsule staged{};
 staged.center1.x=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 staged.center2.y=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 staged.radius=std::bit_cast<float>(uint32_t(r.fields[2].atoms[0].bits));
 if(staged.radius<0)return fail(Error::NonCanonical);
 staged.center1.y=std::bit_cast<float>(uint32_t(r.fields[3].atoms[0].bits));
 staged.center2.x=std::bit_cast<float>(uint32_t(r.fields[4].atoms[0].bits));
 out=staged;return {}; }
Status capture_capsule_geometry(const b2Capsule &native,Identity id,CapsuleGeometryImage &out)noexcept{CapsuleGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.center1.x);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.center2.y);
 staged.atoms[2].bits=std::bit_cast<uint32_t>(native.radius);
 staged.atoms[3].bits=std::bit_cast<uint32_t>(native.center1.y);
 staged.atoms[4].bits=std::bit_cast<uint32_t>(native.center2.x);
 b2Capsule check{};if(auto s=restore_capsule_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,4> segment_defs{{
 {1350381320u,AtomType::Float32,0,1,1,false}, // point1.y
 {1756564999u,AtomType::Float32,0,1,1,false}, // point2.y
 {4018700077u,AtomType::Float32,0,1,1,false}, // point2.x
 {4181138439u,AtomType::Float32,0,1,1,false}, // point1.x
}};}
SegmentGeometryImage::SegmentGeometryImage()noexcept{for(size_t i=0;i<4;++i)fields[i]={segment_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> segment_geometry_fields()noexcept{return segment_defs;}
Status restore_segment_geometry(const Record &r,b2Segment &out)noexcept{if(auto s=validate_geometry(r,segment_geometry_kind,segment_defs);!s)return s;b2Segment staged{};
 staged.point1.y=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 staged.point2.y=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 staged.point2.x=std::bit_cast<float>(uint32_t(r.fields[2].atoms[0].bits));
 staged.point1.x=std::bit_cast<float>(uint32_t(r.fields[3].atoms[0].bits));
 out=staged;return {}; }
Status capture_segment_geometry(const b2Segment &native,Identity id,SegmentGeometryImage &out)noexcept{SegmentGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.point1.y);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.point2.y);
 staged.atoms[2].bits=std::bit_cast<uint32_t>(native.point2.x);
 staged.atoms[3].bits=std::bit_cast<uint32_t>(native.point1.x);
 b2Segment check{};if(auto s=restore_segment_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,9> chain_segment_defs{{
 {72250847u,AtomType::Float32,0,1,1,false}, // ghost1.x
 {1423633764u,AtomType::Float32,0,1,1,false}, // segment.point1.x
 {2282639303u,AtomType::Reference,chain_kind,1,1,false}, // chainId
 {2562561515u,AtomType::Float32,0,1,1,false}, // segment.point2.x
 {2964469567u,AtomType::Float32,0,1,1,false}, // ghost2.x
 {3496700051u,AtomType::Float32,0,1,1,false}, // ghost2.y
 {3725391028u,AtomType::Float32,0,1,1,false}, // ghost1.y
 {4040754125u,AtomType::Float32,0,1,1,false}, // segment.point2.y
 {4262147160u,AtomType::Float32,0,1,1,false}, // segment.point1.y
}};}
ChainSegmentGeometryImage::ChainSegmentGeometryImage()noexcept{for(size_t i=0;i<9;++i)fields[i]={chain_segment_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> chain_segment_geometry_fields()noexcept{return chain_segment_defs;}
Status restore_chain_segment_geometry(const Record &r,const IdentityMap &chains,b2ChainSegment &out)noexcept{if(auto s=validate_geometry(r,chain_segment_geometry_kind,chain_segment_defs);!s)return s;b2ChainSegment staged{};
 staged.ghost1.x=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 staged.segment.point1.x=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 {auto n=chains.native(r.fields[2].atoms[0].identity);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);staged.chainId=int(*n);}
 staged.segment.point2.x=std::bit_cast<float>(uint32_t(r.fields[3].atoms[0].bits));
 staged.ghost2.x=std::bit_cast<float>(uint32_t(r.fields[4].atoms[0].bits));
 staged.ghost2.y=std::bit_cast<float>(uint32_t(r.fields[5].atoms[0].bits));
 staged.ghost1.y=std::bit_cast<float>(uint32_t(r.fields[6].atoms[0].bits));
 staged.segment.point2.y=std::bit_cast<float>(uint32_t(r.fields[7].atoms[0].bits));
 staged.segment.point1.y=std::bit_cast<float>(uint32_t(r.fields[8].atoms[0].bits));
 out=staged;return {}; }
Status capture_chain_segment_geometry(const b2ChainSegment &native,Identity id,const IdentityMap &chains,ChainSegmentGeometryImage &out)noexcept{ChainSegmentGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.ghost1.x);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.segment.point1.x);
 if(native.chainId<0)return fail(Error::InvalidArgument);{auto id=chains.canonical(uint32_t(native.chainId));if(!id||id->kind!=chain_kind)return fail(Error::StaleGeneration);staged.atoms[2].identity=*id;}
 staged.atoms[3].bits=std::bit_cast<uint32_t>(native.segment.point2.x);
 staged.atoms[4].bits=std::bit_cast<uint32_t>(native.ghost2.x);
 staged.atoms[5].bits=std::bit_cast<uint32_t>(native.ghost2.y);
 staged.atoms[6].bits=std::bit_cast<uint32_t>(native.ghost1.y);
 staged.atoms[7].bits=std::bit_cast<uint32_t>(native.segment.point2.y);
 staged.atoms[8].bits=std::bit_cast<uint32_t>(native.segment.point1.y);
 b2ChainSegment check{};if(auto s=restore_chain_segment_geometry(staged.record,chains,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,3> filter_defs{{
 {323224537u,AtomType::Unsigned,0,1,1,false}, // maskBits
 {1190281637u,AtomType::Signed,0,1,1,false}, // groupIndex
 {1566253136u,AtomType::Unsigned,0,1,1,false}, // categoryBits
}};}
FilterGeometryImage::FilterGeometryImage()noexcept{for(size_t i=0;i<3;++i)fields[i]={filter_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> filter_geometry_fields()noexcept{return filter_defs;}
Status restore_filter_geometry(const Record &r,b2Filter &out)noexcept{if(auto s=validate_geometry(r,filter_geometry_kind,filter_defs);!s)return s;b2Filter staged{};
 staged.maskBits=r.fields[0].atoms[0].bits;
 {auto v=std::bit_cast<int64_t>(r.fields[1].atoms[0].bits);if(v<INT_MIN||v>INT_MAX)return fail(Error::InvalidArgument);staged.groupIndex=int(v);}
 staged.categoryBits=r.fields[2].atoms[0].bits;
 out=staged;return {}; }
Status capture_filter_geometry(const b2Filter &native,Identity id,FilterGeometryImage &out)noexcept{FilterGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=native.maskBits;
 staged.atoms[1].bits=std::bit_cast<uint64_t>(int64_t(native.groupIndex));
 staged.atoms[2].bits=native.categoryBits;
 b2Filter check{};if(auto s=restore_filter_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,6> surface_material_defs{{
 {699376458u,AtomType::Float32,0,1,1,false}, // restitution
 {937897884u,AtomType::Float32,0,1,1,false}, // tangentSpeed
 {2235320824u,AtomType::Float32,0,1,1,false}, // friction
 {2750666874u,AtomType::Unsigned,0,1,1,false}, // userMaterialId
 {2998562027u,AtomType::Float32,0,1,1,false}, // rollingResistance
 {4238905871u,AtomType::Unsigned,0,1,1,false}, // customColor
}};}
SurfaceMaterialGeometryImage::SurfaceMaterialGeometryImage()noexcept{for(size_t i=0;i<6;++i)fields[i]={surface_material_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> surface_material_geometry_fields()noexcept{return surface_material_defs;}
Status restore_surface_material_geometry(const Record &r,b2SurfaceMaterial &out)noexcept{if(auto s=validate_geometry(r,surface_material_geometry_kind,surface_material_defs);!s)return s;b2SurfaceMaterial staged{};
 staged.restitution=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 if(staged.restitution<0)return fail(Error::NonCanonical);
 staged.tangentSpeed=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 staged.friction=std::bit_cast<float>(uint32_t(r.fields[2].atoms[0].bits));
 if(staged.friction<0)return fail(Error::NonCanonical);
 staged.userMaterialId=r.fields[3].atoms[0].bits;
 staged.rollingResistance=std::bit_cast<float>(uint32_t(r.fields[4].atoms[0].bits));
 if(staged.rollingResistance<0)return fail(Error::NonCanonical);
 if(r.fields[5].atoms[0].bits>UINT32_MAX)return fail(Error::InvalidArgument);staged.customColor=uint32_t(r.fields[5].atoms[0].bits);
 out=staged;return {}; }
Status capture_surface_material_geometry(const b2SurfaceMaterial &native,Identity id,SurfaceMaterialGeometryImage &out)noexcept{SurfaceMaterialGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.restitution);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.tangentSpeed);
 staged.atoms[2].bits=std::bit_cast<uint32_t>(native.friction);
 staged.atoms[3].bits=native.userMaterialId;
 staged.atoms[4].bits=std::bit_cast<uint32_t>(native.rollingResistance);
 staged.atoms[5].bits=native.customColor;
 b2SurfaceMaterial check{};if(auto s=restore_surface_material_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
namespace {constexpr std::array<FieldSpec,4> aabb_defs{{
 {151055962u,AtomType::Float32,0,1,1,false}, // lowerBound.x
 {1454731370u,AtomType::Float32,0,1,1,false}, // lowerBound.y
 {2078212051u,AtomType::Float32,0,1,1,false}, // upperBound.x
 {3998103970u,AtomType::Float32,0,1,1,false}, // upperBound.y
}};}
AABBGeometryImage::AABBGeometryImage()noexcept{for(size_t i=0;i<4;++i)fields[i]={aabb_defs[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
std::span<const FieldSpec> aabb_geometry_fields()noexcept{return aabb_defs;}
Status restore_aabb_geometry(const Record &r,b2AABB &out)noexcept{if(auto s=validate_geometry(r,aabb_geometry_kind,aabb_defs);!s)return s;b2AABB staged{};
 staged.lowerBound.x=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 staged.lowerBound.y=std::bit_cast<float>(uint32_t(r.fields[1].atoms[0].bits));
 staged.upperBound.x=std::bit_cast<float>(uint32_t(r.fields[2].atoms[0].bits));
 staged.upperBound.y=std::bit_cast<float>(uint32_t(r.fields[3].atoms[0].bits));
 if(staged.lowerBound.x>staged.upperBound.x||staged.lowerBound.y>staged.upperBound.y)return fail(Error::InvalidArgument);
 out=staged;return {}; }
Status capture_aabb_geometry(const b2AABB &native,Identity id,AABBGeometryImage &out)noexcept{AABBGeometryImage staged;staged.record.identity=id;
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.lowerBound.x);
 staged.atoms[1].bits=std::bit_cast<uint32_t>(native.lowerBound.y);
 staged.atoms[2].bits=std::bit_cast<uint32_t>(native.upperBound.x);
 staged.atoms[3].bits=std::bit_cast<uint32_t>(native.upperBound.y);
 b2AABB check{};if(auto s=restore_aabb_geometry(staged.record,check);!s)return s;out.atoms=staged.atoms;out.record.identity=id;return {};}
}
