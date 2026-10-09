// SPDX-License-Identifier: MIT
#include "cold_contact_codec.hpp"
namespace superpos::box2d_portable {namespace {using namespace canonical;
constexpr std::array<FieldSpec,21> definitions{{
 {25269177u,AtomType::Reference,5121,1,1,true}, // setIndex
 {640073543u,AtomType::Reference,4102,1,1,true}, // edges[1].nextKey.reference
 {703682067u,AtomType::Reference,4102,1,1,true}, // islandIndex
 {874448494u,AtomType::Boolean,0,1,1,false}, // colorIndex.present
 {1158985243u,AtomType::Unsigned,0,1,1,false}, // generation
 {1281139965u,AtomType::Unsigned,0,1,1,false}, // colorIndex.ordinal
 {1292243085u,AtomType::Reference,4097,1,1,false}, // edges[1].bodyId
 {1631963170u,AtomType::Unsigned,0,1,1,false}, // edges[0].prevKey.endpoint
 {1632389829u,AtomType::Reference,4102,1,1,true}, // edges[0].nextKey.reference
 {1921626214u,AtomType::Reference,4103,1,1,false}, // shapeIdB
 {2049917993u,AtomType::Reference,4102,1,1,false}, // contactId
 {2124218927u,AtomType::Unsigned,0,1,1,false}, // edges[1].prevKey.endpoint
 {2339630122u,AtomType::Unsigned,0,1,1,false}, // edges[1].nextKey.endpoint
 {2363232837u,AtomType::Reference,4097,1,1,false}, // edges[0].bodyId
 {2567382491u,AtomType::Reference,4102,1,1,true}, // edges[0].prevKey.reference
 {2897246774u,AtomType::Unsigned,0,1,1,false}, // edges[0].nextKey.endpoint
 {2994615650u,AtomType::Reference,4102,1,1,true}, // localIndex
 {3088200130u,AtomType::Reference,4102,1,1,true}, // edges[1].prevKey.reference
 {3107366869u,AtomType::Reference,4103,1,1,false}, // shapeIdA
 {3330863463u,AtomType::Reference,5122,1,1,true}, // islandId
 {4033111681u,AtomType::Unsigned,0,1,1,false}, // flags
}};
Status validate(const Record &r)noexcept{
 if(r.identity.kind!=contact_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=r.fields[i];if(f.id!=d.id||f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>UINT32_MAX||(d.type==AtomType::Boolean&&a.bits>1))return fail(Error::InvalidArgument);}
 return {};
}
}
std::span<const FieldSpec> cold_contact_fields()noexcept{return definitions;}
ColdContactImage::ColdContactImage()noexcept{for(size_t i=0;i<fields.size();++i)fields[i]={definitions[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
Status capture_cold_contact(const SpColdContact &native,Identity identity,const ColdContactMappings &maps,ColdContactImage &image)noexcept{
 ColdContactImage staged;
 if(native.setIndex < -1)return fail(Error::InvalidArgument);if(native.setIndex!=-1){auto ref=maps.solver_set.canonical(std::uint32_t(native.setIndex));if(!ref)return fail(ref.error());staged.atoms[0].identity=*ref;}
 if(native.edges[1].nextKey < -1)return fail(Error::InvalidArgument);if(native.edges[1].nextKey!=-1){auto ref=maps.contact.canonical(std::uint32_t(native.edges[1].nextKey)>>1);if(!ref)return fail(ref.error());staged.atoms[1].identity=*ref;}
 if(native.islandIndex < -1)return fail(Error::InvalidArgument);if(native.islandIndex!=-1){auto ref=maps.island_member.canonical(std::uint32_t(native.islandIndex));if(!ref)return fail(ref.error());staged.atoms[2].identity=*ref;}
 staged.atoms[3].bits=native.colorIndex!=-1;
 staged.atoms[4].bits=native.generation;
 if(native.colorIndex < -1 || native.colorIndex>=spContactColorCount())return fail(Error::InvalidArgument);staged.atoms[5].bits=native.colorIndex==-1?0:std::uint64_t(native.colorIndex);
 if(native.edges[1].bodyId < 0)return fail(Error::InvalidArgument);{auto ref=maps.body.canonical(std::uint32_t(native.edges[1].bodyId));if(!ref)return fail(ref.error());staged.atoms[6].identity=*ref;}
 staged.atoms[7].bits=native.edges[0].prevKey==-1?0:(std::uint32_t(native.edges[0].prevKey)&1);
 if(native.edges[0].nextKey < -1)return fail(Error::InvalidArgument);if(native.edges[0].nextKey!=-1){auto ref=maps.contact.canonical(std::uint32_t(native.edges[0].nextKey)>>1);if(!ref)return fail(ref.error());staged.atoms[8].identity=*ref;}
 if(native.shapeIdB < 0)return fail(Error::InvalidArgument);{auto ref=maps.shape.canonical(std::uint32_t(native.shapeIdB));if(!ref)return fail(ref.error());staged.atoms[9].identity=*ref;}
 if(native.contactId < 0)return fail(Error::InvalidArgument);{auto ref=maps.contact.canonical(std::uint32_t(native.contactId));if(!ref)return fail(ref.error());staged.atoms[10].identity=*ref;}
 staged.atoms[11].bits=native.edges[1].prevKey==-1?0:(std::uint32_t(native.edges[1].prevKey)&1);
 staged.atoms[12].bits=native.edges[1].nextKey==-1?0:(std::uint32_t(native.edges[1].nextKey)&1);
 if(native.edges[0].bodyId < 0)return fail(Error::InvalidArgument);{auto ref=maps.body.canonical(std::uint32_t(native.edges[0].bodyId));if(!ref)return fail(ref.error());staged.atoms[13].identity=*ref;}
 if(native.edges[0].prevKey < -1)return fail(Error::InvalidArgument);if(native.edges[0].prevKey!=-1){auto ref=maps.contact.canonical(std::uint32_t(native.edges[0].prevKey)>>1);if(!ref)return fail(ref.error());staged.atoms[14].identity=*ref;}
 staged.atoms[15].bits=native.edges[0].nextKey==-1?0:(std::uint32_t(native.edges[0].nextKey)&1);
 if(native.localIndex < -1)return fail(Error::InvalidArgument);if(native.localIndex!=-1){auto ref=maps.solver_member.canonical(std::uint32_t(native.localIndex));if(!ref)return fail(ref.error());staged.atoms[16].identity=*ref;}
 if(native.edges[1].prevKey < -1)return fail(Error::InvalidArgument);if(native.edges[1].prevKey!=-1){auto ref=maps.contact.canonical(std::uint32_t(native.edges[1].prevKey)>>1);if(!ref)return fail(ref.error());staged.atoms[17].identity=*ref;}
 if(native.shapeIdA < 0)return fail(Error::InvalidArgument);{auto ref=maps.shape.canonical(std::uint32_t(native.shapeIdA));if(!ref)return fail(ref.error());staged.atoms[18].identity=*ref;}
 if(native.islandId < -1)return fail(Error::InvalidArgument);if(native.islandId!=-1){auto ref=maps.island.canonical(std::uint32_t(native.islandId));if(!ref)return fail(ref.error());staged.atoms[19].identity=*ref;}
 staged.atoms[20].bits=native.flags;
 if(staged.atoms[10].identity!=identity)return fail(Error::StaleGeneration);staged.record.identity=identity;if(auto v=validate(staged.record);!v)return v;image.atoms=staged.atoms;image.record.identity=identity;return {};}
Status restore_cold_contact(const Record &record,const ColdContactMappings &maps,SpColdContact &destination)noexcept{if(auto v=validate(record);!v)return v;SpColdContact staged={};
 if(record.fields[10].atoms[0].identity!=record.identity)return fail(Error::StaleGeneration);
 {const auto &id=record.fields[0].atoms[0].identity;if(!id.kind)staged.setIndex=-1;else{auto slot=maps.solver_set.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.setIndex=int(*slot);}}
 {const auto &id=record.fields[1].atoms[0].identity;auto side=record.fields[12].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[1].nextKey=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.edges[1].nextKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[2].atoms[0].identity;if(!id.kind)staged.islandIndex=-1;else{auto slot=maps.island_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandIndex=int(*slot);}}
 staged.generation=std::uint32_t(record.fields[4].atoms[0].bits);
 {auto ordinal=record.fields[5].atoms[0].bits;auto present=record.fields[3].atoms[0].bits;if((!present&&ordinal)||ordinal>=std::uint64_t(spContactColorCount()))return fail(Error::NonCanonical);staged.colorIndex=present?int(ordinal):-1;}
 {const auto &id=record.fields[6].atoms[0].identity;if(!id.kind)staged.edges[1].bodyId=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.edges[1].bodyId=int(*slot);}}
 {const auto &id=record.fields[8].atoms[0].identity;auto side=record.fields[15].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[0].nextKey=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.edges[0].nextKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[9].atoms[0].identity;if(!id.kind)staged.shapeIdB=-1;else{auto slot=maps.shape.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.shapeIdB=int(*slot);}}
 {const auto &id=record.fields[10].atoms[0].identity;if(!id.kind)staged.contactId=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.contactId=int(*slot);}}
 {const auto &id=record.fields[13].atoms[0].identity;if(!id.kind)staged.edges[0].bodyId=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.edges[0].bodyId=int(*slot);}}
 {const auto &id=record.fields[14].atoms[0].identity;auto side=record.fields[7].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[0].prevKey=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.edges[0].prevKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[16].atoms[0].identity;if(!id.kind)staged.localIndex=-1;else{auto slot=maps.solver_member.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.localIndex=int(*slot);}}
 {const auto &id=record.fields[17].atoms[0].identity;auto side=record.fields[11].atoms[0].bits;if(side>1||(!id.kind&&side))return fail(Error::NonCanonical);if(!id.kind)staged.edges[1].prevKey=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>std::uint32_t(INT32_MAX)>>1)return fail(Error::Overflow);staged.edges[1].prevKey=int((*slot<<1)|std::uint32_t(side));}}
 {const auto &id=record.fields[18].atoms[0].identity;if(!id.kind)staged.shapeIdA=-1;else{auto slot=maps.shape.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.shapeIdA=int(*slot);}}
 {const auto &id=record.fields[19].atoms[0].identity;if(!id.kind)staged.islandId=-1;else{auto slot=maps.island.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.islandId=int(*slot);}}
 staged.flags=std::uint32_t(record.fields[20].atoms[0].bits);
 destination=staged;return {}; }
}
