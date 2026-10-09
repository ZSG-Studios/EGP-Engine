// SPDX-License-Identifier: MIT
#include "manifold_codec.hpp"
#include <bit>
#include <cmath>
namespace superpos::box2d_portable {
namespace {
using namespace canonical;
constexpr std::array<FieldSpec,13> definitions{{
    {265259484u,AtomType::Boolean,0,2,2,false}, // b2Manifold.points.persisted
    {333112368u,AtomType::Unsigned,0,2,2,false}, // b2Manifold.points.id
    {346718355u,AtomType::Unsigned,0,1,1,false}, // b2Manifold.pointCount
    {1145666188u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.normalImpulse
    {1375414146u,AtomType::Float32,0,1,1,false}, // b2Manifold.rollingImpulse
    {1656896140u,AtomType::Float32,0,2,2,false}, // b2Manifold.normal
    {1789944420u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.totalNormalImpulse
    {2413513347u,AtomType::Float32,0,4,4,false}, // b2Manifold.points.anchorB
    {2428126328u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.tangentImpulse
    {2699401392u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.separation
    {2700712340u,AtomType::Float32,0,4,4,false}, // b2Manifold.points.anchorA
    {3566132262u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.normalVelocity
    {4026896057u,AtomType::Float32,0,2,2,false}, // b2Manifold.points.baseSeparation
}};
Atom fp(float v) noexcept {return {std::bit_cast<std::uint32_t>(v),{}};}
float value(const Atom &a) noexcept {return std::bit_cast<float>(std::uint32_t(a.bits));}
}
std::span<const FieldSpec> manifold_fields() noexcept {return definitions;}
ManifoldImage::ManifoldImage() noexcept {
 size_t cursor=0;for(size_t i=0;i<definitions.size();++i){fields[i]={definitions[i].id,std::span(atoms).subspan(cursor,definitions[i].maximum_atoms)};cursor+=definitions[i].maximum_atoms;}
 record.fields=fields;
}
Status capture_manifold(const b2Manifold &native,Identity identity,ManifoldImage &image) noexcept {
 if(identity.kind!=manifold_kind || !identity.simulation || !identity.generation || native.pointCount<0 || native.pointCount>2)return fail(Error::InvalidArgument);
 std::array<Atom,28> staged{};size_t cursor=0;
 for(const auto &p:native.points){staged[cursor++].bits=p.persisted;}
 for(const auto &p:native.points){staged[cursor++].bits=p.id;}
 staged[cursor++].bits=std::uint64_t(native.pointCount);
 for(const auto &p:native.points){staged[cursor++]=fp(p.normalImpulse);}
 staged[cursor++]=fp(native.rollingImpulse);
 staged[cursor++]=fp(native.normal.x);staged[cursor++]=fp(native.normal.y);
 for(const auto &p:native.points){staged[cursor++]=fp(p.totalNormalImpulse);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.anchorB.x);staged[cursor++]=fp(p.anchorB.y);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.tangentImpulse);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.separation);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.anchorA.x);staged[cursor++]=fp(p.anchorA.y);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.normalVelocity);}
 for(const auto &p:native.points){staged[cursor++]=fp(p.baseSeparation);}
 cursor=0;for(const auto &f:definitions)for(unsigned n=0;n<f.maximum_atoms;++n,++cursor)
  if(f.type==AtomType::Float32 && !std::isfinite(value(staged[cursor])))return fail(Error::NonCanonical);
 image.atoms=staged;image.record.identity=identity;return {};
}
Status restore_manifold(const Record &record,b2Manifold &destination) noexcept {
 if(record.identity.kind!=manifold_kind || !record.identity.simulation || !record.identity.generation || record.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &f=record.fields[i];const auto &d=definitions[i];
  if(f.id!=d.id || f.atoms.size()!=d.maximum_atoms)return fail(Error::IncompatibleSchema);
  for(const auto &a:f.atoms){if(a.identity.kind || a.identity.simulation || a.identity.generation)return fail(Error::InvalidArgument);
   if(d.type==AtomType::Float32 && (a.bits>UINT32_MAX || !std::isfinite(value(a))))return fail(Error::NonCanonical);
   if(d.type==AtomType::Boolean && a.bits>1)return fail(Error::NonCanonical);
  }
 }
 b2Manifold staged{};
 for(unsigned p=0;p<2;++p){staged.points[p].persisted=record.fields[0].atoms[p].bits!=0;}
 for(unsigned p=0;p<2;++p){if(record.fields[1].atoms[p].bits>UINT16_MAX)return fail(Error::Overflow);staged.points[p].id=std::uint16_t(record.fields[1].atoms[p].bits);}
 if(record.fields[2].atoms[0].bits>2)return fail(Error::InvalidArgument);staged.pointCount=int(record.fields[2].atoms[0].bits);
 for(unsigned p=0;p<2;++p){staged.points[p].normalImpulse=value(record.fields[3].atoms[p]);}
 staged.rollingImpulse=value(record.fields[4].atoms[0]);
 staged.normal={value(record.fields[5].atoms[0]),value(record.fields[5].atoms[1])};
 for(unsigned p=0;p<2;++p){staged.points[p].totalNormalImpulse=value(record.fields[6].atoms[p]);}
 for(unsigned p=0;p<2;++p){staged.points[p].anchorB={value(record.fields[7].atoms[p*2]),value(record.fields[7].atoms[p*2+1])};}
 for(unsigned p=0;p<2;++p){staged.points[p].tangentImpulse=value(record.fields[8].atoms[p]);}
 for(unsigned p=0;p<2;++p){staged.points[p].separation=value(record.fields[9].atoms[p]);}
 for(unsigned p=0;p<2;++p){staged.points[p].anchorA={value(record.fields[10].atoms[p*2]),value(record.fields[10].atoms[p*2+1])};}
 for(unsigned p=0;p<2;++p){staged.points[p].normalVelocity=value(record.fields[11].atoms[p]);}
 for(unsigned p=0;p<2;++p){staged.points[p].baseSeparation=value(record.fields[12].atoms[p]);}
 destination=staged;return {};
}
}
