// SPDX-License-Identifier: MIT
#include "body_codec.hpp"
#include <bit>
#include <cmath>
#include <type_traits>
namespace superpos::box2d_portable {
namespace {
using namespace canonical;
using PositionScalar=decltype(b2Pos{}.x);
constexpr AtomType position_type=sizeof(PositionScalar)==8?AtomType::Float64:AtomType::Float32;
template<class T> Atom scalar(T v) noexcept {
 if constexpr(sizeof(T)==8)return {std::bit_cast<std::uint64_t>(v),{}};
 else return {std::bit_cast<std::uint32_t>(v),{}};
}
template<class T> T number(const Atom &a) noexcept {
 if constexpr(sizeof(T)==8)return std::bit_cast<T>(a.bits);
 else return std::bit_cast<T>(std::uint32_t(a.bits));
}
constexpr std::array<FieldSpec,5> BodyStateDefinitions{{
 {88701524u,AtomType::Float32,0,2,2,false}, // b2BodyState.linearVelocity
 {895469741u,AtomType::Unsigned,0,1,1,false}, // b2BodyState.flags
 {1559611674u,AtomType::Float32,0,2,2,false}, // b2BodyState.deltaPosition
 {2223318989u,AtomType::Float32,0,2,2,false}, // b2BodyState.deltaRotation
 {2271526293u,AtomType::Float32,0,1,1,false}, // b2BodyState.angularVelocity
}};
constexpr std::array<FieldSpec,17> BodySimDefinitions{{
 {105514401u,position_type,0,2,2,false}, // b2BodySim.center
 {557016107u,AtomType::Float32,0,1,1,false}, // b2BodySim.maxExtent
 {1032068052u,position_type,0,2,2,false}, // b2BodySim.transform.p
 {1396924928u,AtomType::Float32,0,1,1,false}, // b2BodySim.torque
 {1705610067u,position_type,0,2,2,false}, // b2BodySim.center0
 {2061582053u,AtomType::Float32,0,1,1,false}, // b2BodySim.invMass
 {2127292827u,AtomType::Float32,0,2,2,false}, // b2BodySim.transform.q
 {2158640768u,AtomType::Float32,0,1,1,false}, // b2BodySim.invInertia
 {2200037011u,AtomType::Float32,0,2,2,false}, // b2BodySim.localCenter
 {2588907968u,AtomType::Float32,0,1,1,false}, // b2BodySim.linearDamping
 {2743548120u,AtomType::Reference,body_kind,1,1,false}, // b2BodySim.bodyId
 {2885480595u,AtomType::Float32,0,2,2,false}, // b2BodySim.rotation0
 {3201170024u,AtomType::Float32,0,1,1,false}, // b2BodySim.angularDamping
 {3609468649u,AtomType::Unsigned,0,1,1,false}, // b2BodySim.flags
 {3931633230u,AtomType::Float32,0,2,2,false}, // b2BodySim.force
 {3979334753u,AtomType::Float32,0,1,1,false}, // b2BodySim.minExtent
 {4007435774u,AtomType::Float32,0,1,1,false}, // b2BodySim.gravityScale
}};
Status validate(std::span<const FieldSpec> definitions,const Record &record,std::uint32_t kind) noexcept {
 if(record.identity.kind!=kind || !record.identity.simulation || !record.identity.generation || record.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=record.fields[i];
  if(f.id!=d.id || f.atoms.size()!=d.maximum_atoms)return fail(Error::IncompatibleSchema);
  for(const auto &a:f.atoms){if(d.type==AtomType::Reference){if(a.bits || a.identity.kind!=body_kind || !a.identity.simulation || !a.identity.generation)return fail(Error::InvalidArgument);}
   else {if(a.identity.kind || a.identity.simulation || a.identity.generation)return fail(Error::InvalidArgument);
    if(d.type==AtomType::Float32 && (a.bits>UINT32_MAX || !std::isfinite(number<float>(a))))return fail(Error::NonCanonical);
    if(d.type==AtomType::Float64 && !std::isfinite(number<double>(a)))return fail(Error::NonCanonical);
    if(d.type==AtomType::Unsigned && a.bits>UINT32_MAX)return fail(Error::Overflow);
   }
  }
 }
 return {};
}
}
std::span<const FieldSpec> bodystate_fields() noexcept {return BodyStateDefinitions;}
BodyStateImage::BodyStateImage() noexcept {size_t cursor=0;for(size_t i=0;i<fields.size();++i){fields[i]={BodyStateDefinitions[i].id,std::span(atoms).subspan(cursor,BodyStateDefinitions[i].maximum_atoms)};cursor+=BodyStateDefinitions[i].maximum_atoms;}record.fields=fields;}
Status capture_body_state(const b2BodyState &native,Identity identity,BodyStateImage &image) noexcept {
 if(identity.kind!=bodystate_kind || !identity.simulation || !identity.generation)return fail(Error::InvalidArgument);
 BodyStateImage staged;
 size_t cursor=0;
 staged.atoms[cursor++]=scalar(native.linearVelocity.x);
 staged.atoms[cursor++]=scalar(native.linearVelocity.y);
 staged.atoms[cursor++].bits=native.flags;
 staged.atoms[cursor++]=scalar(native.deltaPosition.x);
 staged.atoms[cursor++]=scalar(native.deltaPosition.y);
 staged.atoms[cursor++]=scalar(native.deltaRotation.c);
 staged.atoms[cursor++]=scalar(native.deltaRotation.s);
 staged.atoms[cursor++]=scalar(native.angularVelocity);
 staged.record.identity=identity;if(auto valid=validate(BodyStateDefinitions,staged.record,bodystate_kind);!valid)return valid;
 image.atoms=staged.atoms;image.record.identity=identity;return {};
}
Status restore_body_state(const Record &record,b2BodyState &destination) noexcept {
 if(auto valid=validate(BodyStateDefinitions,record,bodystate_kind);!valid)return valid;
 b2BodyState staged{};
 staged.linearVelocity.x=number<decltype(staged.linearVelocity.x)>(record.fields[0].atoms[0]);
 staged.linearVelocity.y=number<decltype(staged.linearVelocity.y)>(record.fields[0].atoms[1]);
 staged.flags=std::uint32_t(record.fields[1].atoms[0].bits);
 staged.deltaPosition.x=number<decltype(staged.deltaPosition.x)>(record.fields[2].atoms[0]);
 staged.deltaPosition.y=number<decltype(staged.deltaPosition.y)>(record.fields[2].atoms[1]);
 staged.deltaRotation.c=number<decltype(staged.deltaRotation.c)>(record.fields[3].atoms[0]);
 staged.deltaRotation.s=number<decltype(staged.deltaRotation.s)>(record.fields[3].atoms[1]);
 staged.angularVelocity=number<decltype(staged.angularVelocity)>(record.fields[4].atoms[0]);
 destination=staged;return {};
}
std::span<const FieldSpec> bodysim_fields() noexcept {return BodySimDefinitions;}
BodySimImage::BodySimImage() noexcept {size_t cursor=0;for(size_t i=0;i<fields.size();++i){fields[i]={BodySimDefinitions[i].id,std::span(atoms).subspan(cursor,BodySimDefinitions[i].maximum_atoms)};cursor+=BodySimDefinitions[i].maximum_atoms;}record.fields=fields;}
Status capture_body_sim(const b2BodySim &native,Identity identity,const IdentityMap &mapping,BodySimImage &image) noexcept {
 if(identity.kind!=bodysim_kind || !identity.simulation || !identity.generation)return fail(Error::InvalidArgument);
 BodySimImage staged;
 size_t cursor=0;
 staged.atoms[cursor++]=scalar(native.center.x);
 staged.atoms[cursor++]=scalar(native.center.y);
 staged.atoms[cursor++]=scalar(native.maxExtent);
 staged.atoms[cursor++]=scalar(native.transform.p.x);
 staged.atoms[cursor++]=scalar(native.transform.p.y);
 staged.atoms[cursor++]=scalar(native.torque);
 staged.atoms[cursor++]=scalar(native.center0.x);
 staged.atoms[cursor++]=scalar(native.center0.y);
 staged.atoms[cursor++]=scalar(native.invMass);
 staged.atoms[cursor++]=scalar(native.transform.q.c);
 staged.atoms[cursor++]=scalar(native.transform.q.s);
 staged.atoms[cursor++]=scalar(native.invInertia);
 staged.atoms[cursor++]=scalar(native.localCenter.x);
 staged.atoms[cursor++]=scalar(native.localCenter.y);
 staged.atoms[cursor++]=scalar(native.linearDamping);
 if(native.bodyId<0)return fail(Error::InvalidArgument);{auto id=mapping.canonical(std::uint32_t(native.bodyId));if(!id)return fail(id.error());staged.atoms[cursor++].identity=*id;}
 staged.atoms[cursor++]=scalar(native.rotation0.c);
 staged.atoms[cursor++]=scalar(native.rotation0.s);
 staged.atoms[cursor++]=scalar(native.angularDamping);
 staged.atoms[cursor++].bits=native.flags;
 staged.atoms[cursor++]=scalar(native.force.x);
 staged.atoms[cursor++]=scalar(native.force.y);
 staged.atoms[cursor++]=scalar(native.minExtent);
 staged.atoms[cursor++]=scalar(native.gravityScale);
 staged.record.identity=identity;if(auto valid=validate(BodySimDefinitions,staged.record,bodysim_kind);!valid)return valid;
 image.atoms=staged.atoms;image.record.identity=identity;return {};
}
Status restore_body_sim(const Record &record,const IdentityMap &mapping,b2BodySim &destination) noexcept {
 if(auto valid=validate(BodySimDefinitions,record,bodysim_kind);!valid)return valid;
 b2BodySim staged{};
 staged.center.x=number<decltype(staged.center.x)>(record.fields[0].atoms[0]);
 staged.center.y=number<decltype(staged.center.y)>(record.fields[0].atoms[1]);
 staged.maxExtent=number<decltype(staged.maxExtent)>(record.fields[1].atoms[0]);
 staged.transform.p.x=number<decltype(staged.transform.p.x)>(record.fields[2].atoms[0]);
 staged.transform.p.y=number<decltype(staged.transform.p.y)>(record.fields[2].atoms[1]);
 staged.torque=number<decltype(staged.torque)>(record.fields[3].atoms[0]);
 staged.center0.x=number<decltype(staged.center0.x)>(record.fields[4].atoms[0]);
 staged.center0.y=number<decltype(staged.center0.y)>(record.fields[4].atoms[1]);
 staged.invMass=number<decltype(staged.invMass)>(record.fields[5].atoms[0]);
 staged.transform.q.c=number<decltype(staged.transform.q.c)>(record.fields[6].atoms[0]);
 staged.transform.q.s=number<decltype(staged.transform.q.s)>(record.fields[6].atoms[1]);
 staged.invInertia=number<decltype(staged.invInertia)>(record.fields[7].atoms[0]);
 staged.localCenter.x=number<decltype(staged.localCenter.x)>(record.fields[8].atoms[0]);
 staged.localCenter.y=number<decltype(staged.localCenter.y)>(record.fields[8].atoms[1]);
 staged.linearDamping=number<decltype(staged.linearDamping)>(record.fields[9].atoms[0]);
 {auto slot=mapping.native(record.fields[10].atoms[0].identity);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyId=int(*slot);}
 staged.rotation0.c=number<decltype(staged.rotation0.c)>(record.fields[11].atoms[0]);
 staged.rotation0.s=number<decltype(staged.rotation0.s)>(record.fields[11].atoms[1]);
 staged.angularDamping=number<decltype(staged.angularDamping)>(record.fields[12].atoms[0]);
 staged.flags=std::uint32_t(record.fields[13].atoms[0].bits);
 staged.force.x=number<decltype(staged.force.x)>(record.fields[14].atoms[0]);
 staged.force.y=number<decltype(staged.force.y)>(record.fields[14].atoms[1]);
 staged.minExtent=number<decltype(staged.minExtent)>(record.fields[15].atoms[0]);
 staged.gravityScale=number<decltype(staged.gravityScale)>(record.fields[16].atoms[0]);
 destination=staged;return {};
}
}
