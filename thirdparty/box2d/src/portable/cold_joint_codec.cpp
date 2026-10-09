// SPDX-License-Identifier: MIT
#include "cold_joint_codec.hpp"
#include <bit>
#include <cmath>
namespace superpos::box2d_portable {namespace {using namespace canonical;
constexpr std::array<FieldSpec,22> definitions{{
 {441660046u,AtomType::Reference,4104,1,1,true}, // edges[1].prevKey.reference
 {597873607u,AtomType::Boolean,0,1,1,false}, // collideConnected
 {1095819719u,AtomType::Unsigned,0,1,1,false}, // colorIndex.ordinal
 {1619780427u,AtomType::Unsigned,0,1,1,false}, // edges[1].nextKey.endpoint
 {1748054479u,AtomType::Reference,5121,1,1,true}, // setIndex
 {1877010209u,AtomType::Reference,5122,1,1,true}, // islandId
 {1907188603u,AtomType::Unsigned,0,1,1,false}, // edges[1].prevKey.endpoint
 {1936563003u,AtomType::Unsigned,0,1,1,false}, // type
 {2897785174u,AtomType::Unsigned,0,1,1,false}, // edges[0].nextKey.endpoint
 {2967417062u,AtomType::Reference,4104,1,1,true}, // edges[1].nextKey.reference
 {3117601849u,AtomType::Reference,6145,1,1,true}, // userData
 {3223562633u,AtomType::Unsigned,0,1,1,false}, // edges[0].prevKey.endpoint
 {3374621040u,AtomType::Unsigned,0,1,1,false}, // generation
 {3415651069u,AtomType::Float32,0,1,1,false}, // drawScale
 {3487356712u,AtomType::Reference,4104,1,1,true}, // edges[0].prevKey.reference
 {3566192168u,AtomType::Reference,4104,1,1,true}, // localIndex
 {3650062758u,AtomType::Boolean,0,1,1,false}, // colorIndex.present
 {3658397970u,AtomType::Reference,4104,1,1,true}, // islandIndex
 {3780596289u,AtomType::Reference,4097,1,1,false}, // edges[0].bodyId
 {3821490503u,AtomType::Reference,4104,1,1,true}, // edges[0].nextKey.reference
 {3909223801u,AtomType::Reference,4104,1,1,false}, // jointId
 {4233379148u,AtomType::Reference,4097,1,1,false}, // edges[1].bodyId
}};
float value(const Atom &a)noexcept{return std::bit_cast<float>(std::uint32_t(a.bits));}
Status validate(const Record &r)noexcept{if(r.identity.kind!=joint_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=r.fields[i];if(f.id!=d.id||f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else{if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>UINT32_MAX)return fail(Error::InvalidArgument);if(d.type==AtomType::Boolean&&a.bits>1)return fail(Error::NonCanonical);if(d.type==AtomType::Float32&&(!std::isfinite(value(a))||value(a)<0))return fail(Error::NonCanonical);}}
 return {};
}
}
std::span<const FieldSpec> cold_joint_fields()noexcept{return definitions;}
ColdJointImage::ColdJointImage()noexcept{for(size_t i=0;i<fields.size();++i)fields[i]={definitions[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
Status capture_cold_joint(const SpColdJoint &native,Identity identity,const ColdJointMappings &maps,ColdJointImage &image)noexcept{ColdJointImage staged;
 if(native.edges[1].prevKey < -1)return fail(Error::InvalidArgument);if(native.edges[1].prevKey!=-1){auto ref=maps.joint.canonical(std::uint32_t(native.edges[1].prevKey)>>1);if(!ref)return fail(ref.error());staged.atoms[0].identity=*ref;}
 staged.atoms[1].bits=std::uint32_t(native.collideConnected);
 if(native.colorIndex < -1||native.colorIndex>=spContactColorCount())return fail(Error::InvalidArgument);staged.atoms[2].bits=native.colorIndex==-1?0:std::uint32_t(native.colorIndex);
 staged.atoms[3].bits=native.edges[1].nextKey==-1?0:(std::uint32_t(native.edges[1].nextKey)&1);
 if(native.setIndex < -1)return fail(Error::InvalidArgument);if(native.setIndex!=-1){auto ref=maps.solver_set.canonical(std::uint32_t(native.setIndex));if(!ref)return fail(ref.error());staged.atoms[4].identity=*ref;}
 if(native.islandId < -1)return fail(Error::InvalidArgument);if(native.islandId!=-1){auto ref=maps.island.canonical(std::uint32_t(native.islandId));if(!ref)return fail(ref.error());staged.atoms[5].identity=*ref;}
 staged.atoms[6].bits=native.edges[1].prevKey==-1?0:(std::uint32_t(native.edges[1].prevKey)&1);
 staged.atoms[7].bits=std::uint32_t(native.type);
 staged.atoms[8].bits=native.edges[0].nextKey==-1?0:(std::uint32_t(native.edges[0].nextKey)&1);
 if(native.edges[1].nextKey < -1)return fail(Error::InvalidArgument);if(native.edges[1].nextKey!=-1){auto ref=maps.joint.canonical(std::uint32_t(native.edges[1].nextKey)>>1);if(!ref)return fail(ref.error());staged.atoms[9].identity=*ref;}
 {auto ref=maps.binding.canonical(native.userData);if(!ref)return fail(ref.error());staged.atoms[10].identity=*ref;}
 staged.atoms[11].bits=native.edges[0].prevKey==-1?0:(std::uint32_t(native.edges[0].prevKey)&1);
 staged.atoms[12].bits=std::uint32_t(native.generation);
 staged.atoms[13].bits=std::bit_cast<std::uint32_t>(native.drawScale);
 if(native.edges[0].prevKey < -1)return fail(Error::InvalidArgument);if(native.edges[0].prevKey!=-1){auto ref=maps.joint.canonical(std::uint32_t(native.edges[0].prevKey)>>1);if(!ref)return fail(ref.error());staged.atoms[14].identity=*ref;}
 if(native.localIndex < -1)return fail(Error::InvalidArgument);if(native.localIndex!=-1){auto ref=maps.solver_member.canonical(std::uint32_t(native.localIndex));if(!ref)return fail(ref.error());staged.atoms[15].identity=*ref;}
 staged.atoms[16].bits=native.colorIndex!=-1;
 if(native.islandIndex < -1)return fail(Error::InvalidArgument);if(native.islandIndex!=-1){auto ref=maps.island_member.canonical(std::uint32_t(native.islandIndex));if(!ref)return fail(ref.error());staged.atoms[17].identity=*ref;}
 if(native.edges[0].bodyId < 0)return fail(Error::InvalidArgument);{auto ref=maps.body.canonical(std::uint32_t(native.edges[0].bodyId));if(!ref)return fail(ref.error());staged.atoms[18].identity=*ref;}
 if(native.edges[0].nextKey < -1)return fail(Error::InvalidArgument);if(native.edges[0].nextKey!=-1){auto ref=maps.joint.canonical(std::uint32_t(native.edges[0].nextKey)>>1);if(!ref)return fail(ref.error());staged.atoms[19].identity=*ref;}
 if(native.jointId < 0)return fail(Error::InvalidArgument);{auto ref=maps.joint.canonical(std::uint32_t(native.jointId));if(!ref)return fail(ref.error());staged.atoms[20].identity=*ref;}
 if(native.edges[1].bodyId < 0)return fail(Error::InvalidArgument);{auto ref=maps.body.canonical(std::uint32_t(native.edges[1].bodyId));if(!ref)return fail(ref.error());staged.atoms[21].identity=*ref;}
 if(staged.atoms[20].identity!=identity)return fail(Error::StaleGeneration);staged.record.identity=identity;SpColdJoint check{};if(auto v=restore_cold_joint(staged.record,maps,check);!v)return v;image.atoms=staged.atoms;image.record.identity=identity;return {};}
 Status restore_cold_joint(const Record &record,const ColdJointMappings &maps,SpColdJoint &destination)noexcept{if(auto v=validate(record);!v)return v;if(record.fields[20].atoms[0].identity!=record.identity)return fail(Error::StaleGeneration);SpColdJoint staged={};
 {const auto &id=record.fields[0].atoms[0].identity;auto side=record.fields[6].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[1].prevKey=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>(std::uint32_t(INT32_MAX)>>1))return fail(Error::Overflow);staged.edges[1].prevKey=int((*slot<<1)|std::uint32_t(side));}}
 if(record.fields[1].atoms[0].bits>1u)return fail(Error::InvalidArgument);staged.collideConnected=static_cast<decltype(staged.collideConnected)>(record.fields[1].atoms[0].bits);
 {auto v=record.fields[2].atoms[0].bits,present=record.fields[16].atoms[0].bits;if((!present&&v)||v>=std::uint64_t(spContactColorCount()))return fail(Error::NonCanonical);staged.colorIndex=present?int(v):-1;}
 {const auto &id=record.fields[4].atoms[0].identity;if(!id.kind)staged.setIndex=-1;else{auto slot=maps.solver_set.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.setIndex=int(*slot);}}
 {const auto &id=record.fields[5].atoms[0].identity;if(!id.kind)staged.islandId=-1;else{auto slot=maps.island.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandId=int(*slot);}}
 if(record.fields[7].atoms[0].bits>6u)return fail(Error::InvalidArgument);staged.type=static_cast<decltype(staged.type)>(record.fields[7].atoms[0].bits);
 {const auto &id=record.fields[9].atoms[0].identity;auto side=record.fields[3].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[1].nextKey=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>(std::uint32_t(INT32_MAX)>>1))return fail(Error::Overflow);staged.edges[1].nextKey=int((*slot<<1)|std::uint32_t(side));}}
 {auto ptr=maps.binding.native(record.fields[10].atoms[0].identity);if(!ptr)return fail(ptr.error());staged.userData=*ptr;}
 if(record.fields[12].atoms[0].bits>65535u)return fail(Error::InvalidArgument);staged.generation=static_cast<decltype(staged.generation)>(record.fields[12].atoms[0].bits);
 staged.drawScale=value(record.fields[13].atoms[0]);
 {const auto &id=record.fields[14].atoms[0].identity;auto side=record.fields[11].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[0].prevKey=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>(std::uint32_t(INT32_MAX)>>1))return fail(Error::Overflow);staged.edges[0].prevKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[15].atoms[0].identity;if(!id.kind)staged.localIndex=-1;else{auto slot=maps.solver_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.localIndex=int(*slot);}}
 {const auto &id=record.fields[17].atoms[0].identity;if(!id.kind)staged.islandIndex=-1;else{auto slot=maps.island_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandIndex=int(*slot);}}
 {const auto &id=record.fields[18].atoms[0].identity;if(!id.kind)staged.edges[0].bodyId=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.edges[0].bodyId=int(*slot);}}
 {const auto &id=record.fields[19].atoms[0].identity;auto side=record.fields[8].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[0].nextKey=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>(std::uint32_t(INT32_MAX)>>1))return fail(Error::Overflow);staged.edges[0].nextKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[20].atoms[0].identity;if(!id.kind)staged.jointId=-1;else{auto slot=maps.joint.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.jointId=int(*slot);}}
 {const auto &id=record.fields[21].atoms[0].identity;if(!id.kind)staged.edges[1].bodyId=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.edges[1].bodyId=int(*slot);}}
 if(!staged.generation)return fail(Error::InvalidArgument);destination=staged;return {}; }
}
