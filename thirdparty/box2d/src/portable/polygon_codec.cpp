// SPDX-License-Identifier: MIT
#include "polygon_codec.hpp"
#include <bit>
#include <cmath>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr size_t normals_field=0;
constexpr size_t radius_field=1;
constexpr size_t count_field=2;
constexpr size_t vertices_field=3;
constexpr size_t centroid_field=4;
constexpr std::array<FieldSpec,5> defs{{
 {495457478u,AtomType::Float32,0,6,16,false}, // normals
 {1043114241u,AtomType::Float32,0,1,1,false}, // radius
 {1885673183u,AtomType::Unsigned,0,1,1,false}, // count
 {3593911926u,AtomType::Float32,0,6,16,false}, // vertices
 {4093825199u,AtomType::Float32,0,2,2,false}, // centroid
}};
float number(const Atom &a)noexcept{return std::bit_cast<float>(uint32_t(a.bits));}
}
std::span<const FieldSpec> polygon_geometry_fields()noexcept{return defs;}
PolygonGeometryImage::PolygonGeometryImage()noexcept{
 for(size_t i=0;i<5;++i)fields[i].id=defs[i].id;
 fields[vertices_field].atoms=std::span(atoms).subspan(0,6);fields[normals_field].atoms=std::span(atoms).subspan(16,6);
 fields[centroid_field].atoms=std::span(atoms).subspan(32,2);fields[radius_field].atoms=std::span(atoms).subspan(34,1);fields[count_field].atoms=std::span(atoms).subspan(35,1);record.fields=fields;
}
Status restore_polygon_geometry(const Record &r,b2Polygon &out)noexcept{
 if(r.identity.kind!=polygon_geometry_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=5)return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<5;++i){const auto &f=r.fields[i];const auto &d=defs[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);for(const auto &a:f.atoms){if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>UINT32_MAX)return fail(Error::InvalidArgument);if(i!=count_field&&!std::isfinite(number(a)))return fail(Error::NonCanonical);}}
 auto count=r.fields[count_field].atoms[0].bits;if(count<3||count>B2_MAX_POLYGON_VERTICES||r.fields[vertices_field].atoms.size()!=count*2||r.fields[normals_field].atoms.size()!=count*2)return fail(Error::InvalidArgument);
 b2Polygon staged{};staged.count=int(count);staged.radius=number(r.fields[radius_field].atoms[0]);if(staged.radius<0)return fail(Error::InvalidArgument);
 staged.centroid={number(r.fields[centroid_field].atoms[0]),number(r.fields[centroid_field].atoms[1])};
 for(size_t i=0;i<count;++i){staged.vertices[i]={number(r.fields[vertices_field].atoms[i*2]),number(r.fields[vertices_field].atoms[i*2+1])};staged.normals[i]={number(r.fields[normals_field].atoms[i*2]),number(r.fields[normals_field].atoms[i*2+1])};}
 out=staged;return {};
}
Status capture_polygon_geometry(const b2Polygon &native,Identity id,PolygonGeometryImage &out)noexcept{
 if(native.count<3||native.count>B2_MAX_POLYGON_VERTICES)return fail(Error::InvalidArgument);PolygonGeometryImage staged;const auto count=size_t(native.count);
 for(size_t i=0;i<count;++i){staged.atoms[i*2].bits=std::bit_cast<uint32_t>(native.vertices[i].x);staged.atoms[i*2+1].bits=std::bit_cast<uint32_t>(native.vertices[i].y);staged.atoms[16+i*2].bits=std::bit_cast<uint32_t>(native.normals[i].x);staged.atoms[17+i*2].bits=std::bit_cast<uint32_t>(native.normals[i].y);}
 staged.atoms[32].bits=std::bit_cast<uint32_t>(native.centroid.x);staged.atoms[33].bits=std::bit_cast<uint32_t>(native.centroid.y);staged.atoms[34].bits=std::bit_cast<uint32_t>(native.radius);staged.atoms[35].bits=count;
 staged.fields[vertices_field].atoms=std::span(staged.atoms).subspan(0,count*2);staged.fields[normals_field].atoms=std::span(staged.atoms).subspan(16,count*2);staged.record.identity=id;
 b2Polygon checked{};if(auto s=restore_polygon_geometry(staged.record,checked);!s)return s;
 out.atoms=staged.atoms;out.fields[vertices_field].atoms=std::span(out.atoms).subspan(0,count*2);out.fields[normals_field].atoms=std::span(out.atoms).subspan(16,count*2);out.record.identity=id;return {};
}
}
