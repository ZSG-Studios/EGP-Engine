// SPDX-License-Identifier: MIT
#include "cold_body_codec.hpp"
#include <bit>
#include <cmath>
#include <cstring>
namespace superpos::box2d_portable {namespace {using namespace canonical;
static_assert(B2_NAME_LENGTH==10,"requalify body name bounds");
constexpr std::array<FieldSpec,24> definitions{{
 {10027195u,AtomType::Float32,0,1,1,false}, // inertia
 {247824836u,AtomType::Reference,4097,1,1,true}, // localIndex
 {556642969u,AtomType::Unsigned,0,1,1,false}, // headContactKey.endpoint
 {745595149u,AtomType::Reference,4097,1,1,false}, // id
 {1150226404u,AtomType::Unsigned,0,1,1,false}, // shapeCount
 {1246168007u,AtomType::Reference,4097,1,1,true}, // islandIndex
 {1308736818u,AtomType::Unsigned,0,1,1,false}, // headJointKey.endpoint
 {1317461847u,AtomType::Unsigned,0,0,10,false}, // name
 {1655005248u,AtomType::Float32,0,1,1,false}, // sleepThreshold
 {2230273970u,AtomType::Reference,5889,1,1,true}, // bodyMoveIndex
 {2231404335u,AtomType::Unsigned,0,1,1,false}, // type
 {2339783789u,AtomType::Reference,4102,1,1,true}, // headContactKey.reference
 {2479416252u,AtomType::Reference,6145,1,1,true}, // userData
 {2644364315u,AtomType::Float32,0,1,1,false}, // sleepTime
 {2772066994u,AtomType::Reference,4103,1,1,true}, // headShapeId
 {2805097737u,AtomType::Reference,5122,1,1,true}, // islandId
 {2836916756u,AtomType::Reference,4105,1,1,true}, // headChainId
 {2869693988u,AtomType::Unsigned,0,1,1,false}, // contactCount
 {3024108604u,AtomType::Reference,4104,1,1,true}, // headJointKey.reference
 {3235492183u,AtomType::Unsigned,0,1,1,false}, // flags
 {3326788572u,AtomType::Reference,5121,1,1,true}, // setIndex
 {3504037282u,AtomType::Unsigned,0,1,1,false}, // generation
 {3583686260u,AtomType::Float32,0,1,1,false}, // mass
 {4166837506u,AtomType::Unsigned,0,1,1,false}, // jointCount
}};
float value(const Atom &a)noexcept{return std::bit_cast<float>(std::uint32_t(a.bits));}
Status validate(const Record &r)noexcept{
 if(r.identity.kind!=body_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=r.fields[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);
 for(const auto &a:f.atoms){if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;
 if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else{if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>UINT32_MAX)return fail(Error::InvalidArgument);if(d.type==AtomType::Float32&&!std::isfinite(value(a)))return fail(Error::NonCanonical);}}}
 return {};
}
}
std::span<const FieldSpec> cold_body_fields()noexcept{return definitions;}
ColdBodyImage::ColdBodyImage()noexcept{size_t cursor=0;for(size_t i=0;i<fields.size();++i){fields[i]={definitions[i].id,std::span(atoms).subspan(cursor,definitions[i].maximum_atoms)};cursor+=definitions[i].maximum_atoms;}record.fields=fields;}
Status capture_cold_body(const b2Body &native,Identity identity,const ColdBodyMappings &maps,ColdBodyImage &image)noexcept{
 ColdBodyImage staged;size_t cursor=0;
 if(native.inertia<0)return fail(Error::InvalidArgument);staged.atoms[cursor++].bits=std::bit_cast<std::uint32_t>(native.inertia);
 if(native.localIndex < -1)return fail(Error::InvalidArgument);if(native.localIndex!=-1){auto ref=maps.solver_member.canonical(std::uint32_t(native.localIndex));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=native.headContactKey==-1?0:(std::uint32_t(native.headContactKey)&1);
 if(native.id < 0)return fail(Error::InvalidArgument);{auto ref=maps.body.canonical(std::uint32_t(native.id));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=std::uint32_t(native.shapeCount);
 if(native.islandIndex < -1)return fail(Error::InvalidArgument);if(native.islandIndex!=-1){auto ref=maps.island_member.canonical(std::uint32_t(native.islandIndex));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=native.headJointKey==-1?0:(std::uint32_t(native.headJointKey)&1);
 {const auto *end=static_cast<const char *>(std::memchr(native.name,0,sizeof(native.name)));if(!end)return fail(Error::InvalidArgument);auto length=size_t(end-native.name);for(size_t j=0;j<length;++j)staged.atoms[cursor+j].bits=static_cast<unsigned char>(native.name[j]);staged.fields[7].atoms=staged.fields[7].atoms.first(length);cursor+=10;}
 if(native.sleepThreshold<0)return fail(Error::InvalidArgument);staged.atoms[cursor++].bits=std::bit_cast<std::uint32_t>(native.sleepThreshold);
 if(native.bodyMoveIndex < -1)return fail(Error::InvalidArgument);if(native.bodyMoveIndex!=-1){auto ref=maps.move_event.canonical(std::uint32_t(native.bodyMoveIndex));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=std::uint32_t(native.type);
 if(native.headContactKey < -1)return fail(Error::InvalidArgument);if(native.headContactKey!=-1){auto ref=maps.contact.canonical(std::uint32_t(native.headContactKey)>>1);if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 {auto ref=maps.binding.canonical(native.userData);if(!ref)return fail(ref.error());staged.atoms[cursor++].identity=*ref;}
 if(native.sleepTime<0)return fail(Error::InvalidArgument);staged.atoms[cursor++].bits=std::bit_cast<std::uint32_t>(native.sleepTime);
 if(native.headShapeId < -1)return fail(Error::InvalidArgument);if(native.headShapeId!=-1){auto ref=maps.shape.canonical(std::uint32_t(native.headShapeId));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 if(native.islandId < -1)return fail(Error::InvalidArgument);if(native.islandId!=-1){auto ref=maps.island.canonical(std::uint32_t(native.islandId));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 if(native.headChainId < -1)return fail(Error::InvalidArgument);if(native.headChainId!=-1){auto ref=maps.chain.canonical(std::uint32_t(native.headChainId));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=std::uint32_t(native.contactCount);
 if(native.headJointKey < -1)return fail(Error::InvalidArgument);if(native.headJointKey!=-1){auto ref=maps.joint.canonical(std::uint32_t(native.headJointKey)>>1);if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=std::uint32_t(native.flags);
 if(native.setIndex < -1)return fail(Error::InvalidArgument);if(native.setIndex!=-1){auto ref=maps.solver_set.canonical(std::uint32_t(native.setIndex));if(!ref)return fail(ref.error());staged.atoms[cursor].identity=*ref;}++cursor;
 staged.atoms[cursor++].bits=std::uint32_t(native.generation);
 if(native.mass<0)return fail(Error::InvalidArgument);staged.atoms[cursor++].bits=std::bit_cast<std::uint32_t>(native.mass);
 staged.atoms[cursor++].bits=std::uint32_t(native.jointCount);
 if(staged.fields[3].atoms[0].identity!=identity)return fail(Error::StaleGeneration);staged.record.identity=identity;
 if(auto v=validate(staged.record);!v)return v;
 b2Body checked{};if(auto v=restore_cold_body(staged.record,maps,checked);!v)return v;
 image.atoms=staged.atoms;size_t offset=0;for(size_t i=0;i<image.fields.size();++i){image.fields[i].atoms=std::span(image.atoms).subspan(offset,staged.fields[i].atoms.size());offset+=definitions[i].maximum_atoms;}image.record.identity=identity;return {};
}
Status restore_cold_body(const Record &record,const ColdBodyMappings &maps,b2Body &destination)noexcept{
 if(auto v=validate(record);!v)return v;if(record.fields[3].atoms[0].identity!=record.identity)return fail(Error::StaleGeneration);b2Body staged={};
 staged.inertia=value(record.fields[0].atoms[0]);if(staged.inertia<0)return fail(Error::InvalidArgument);
 {const auto &id=record.fields[1].atoms[0].identity;if(!id.kind)staged.localIndex=-1;else{auto slot=maps.solver_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.localIndex=int(*slot);}}
 {const auto &id=record.fields[3].atoms[0].identity;if(!id.kind)staged.id=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.id=int(*slot);}}
 if(record.fields[4].atoms[0].bits>100000u)return fail(Error::InvalidArgument);staged.shapeCount=static_cast<decltype(staged.shapeCount)>(record.fields[4].atoms[0].bits);
 {const auto &id=record.fields[5].atoms[0].identity;if(!id.kind)staged.islandIndex=-1;else{auto slot=maps.island_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandIndex=int(*slot);}}
 for(size_t j=0;j<record.fields[7].atoms.size();++j){auto b=record.fields[7].atoms[j].bits;if(!b||b>255)return fail(Error::NonCanonical);staged.name[j]=char(static_cast<unsigned char>(b));}
 staged.sleepThreshold=value(record.fields[8].atoms[0]);if(staged.sleepThreshold<0)return fail(Error::InvalidArgument);
 {const auto &id=record.fields[9].atoms[0].identity;if(!id.kind)staged.bodyMoveIndex=-1;else{auto slot=maps.move_event.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyMoveIndex=int(*slot);}}
 if(record.fields[10].atoms[0].bits>2u)return fail(Error::InvalidArgument);staged.type=static_cast<decltype(staged.type)>(record.fields[10].atoms[0].bits);
 {const auto &id=record.fields[11].atoms[0].identity;auto side=record.fields[2].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.headContactKey=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.headContactKey=int((*slot<<1)|std::uint32_t(side));}}
 {auto pointer=maps.binding.native(record.fields[12].atoms[0].identity);if(!pointer)return fail(pointer.error());staged.userData=*pointer;}
 staged.sleepTime=value(record.fields[13].atoms[0]);if(staged.sleepTime<0)return fail(Error::InvalidArgument);
 {const auto &id=record.fields[14].atoms[0].identity;if(!id.kind)staged.headShapeId=-1;else{auto slot=maps.shape.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.headShapeId=int(*slot);}}
 {const auto &id=record.fields[15].atoms[0].identity;if(!id.kind)staged.islandId=-1;else{auto slot=maps.island.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandId=int(*slot);}}
 {const auto &id=record.fields[16].atoms[0].identity;if(!id.kind)staged.headChainId=-1;else{auto slot=maps.chain.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.headChainId=int(*slot);}}
 if(record.fields[17].atoms[0].bits>100000u)return fail(Error::InvalidArgument);staged.contactCount=static_cast<decltype(staged.contactCount)>(record.fields[17].atoms[0].bits);
 {const auto &id=record.fields[18].atoms[0].identity;auto side=record.fields[6].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.headJointKey=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.headJointKey=int((*slot<<1)|std::uint32_t(side));}}
 if(record.fields[19].atoms[0].bits>8191u)return fail(Error::InvalidArgument);staged.flags=static_cast<decltype(staged.flags)>(record.fields[19].atoms[0].bits);
 {const auto &id=record.fields[20].atoms[0].identity;if(!id.kind)staged.setIndex=-1;else{auto slot=maps.solver_set.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.setIndex=int(*slot);}}
 if(record.fields[21].atoms[0].bits>65535u)return fail(Error::InvalidArgument);staged.generation=static_cast<decltype(staged.generation)>(record.fields[21].atoms[0].bits);
 staged.mass=value(record.fields[22].atoms[0]);if(staged.mass<0)return fail(Error::InvalidArgument);
 if(record.fields[23].atoms[0].bits>100000u)return fail(Error::InvalidArgument);staged.jointCount=static_cast<decltype(staged.jointCount)>(record.fields[23].atoms[0].bits);
 if(!staged.generation)return fail(Error::InvalidArgument);destination=staged;return {}; }
}
