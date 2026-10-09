// SPDX-License-Identifier: MIT
#include "contact_codec.hpp"
#include <bit>
#include <cmath>
#include <type_traits>
namespace superpos::box2d_portable {
namespace {using namespace canonical;
constexpr std::array<FieldSpec,59> definitions{{
 {25563659u,AtomType::Reference,4097,1,1,true}, // bodySimIndexA
 {157083287u,AtomType::Float32,0,1,1,false}, // manifold.points[0].separation
 {315432897u,AtomType::Float32,0,1,1,false}, // manifold.points[0].baseSeparation
 {414586723u,AtomType::Float32,0,1,1,false}, // friction
 {568297766u,AtomType::Reference,4097,1,1,true}, // bodySimIndexB
 {578428191u,AtomType::Float32,0,1,1,false}, // cachedRotationA.c
 {595551340u,AtomType::Float32,0,1,1,false}, // invIB
 {717852523u,AtomType::Float32,0,1,1,false}, // manifold.points[1].anchorB.y
 {789160238u,AtomType::Unsigned,0,1,1,false}, // manifold.pointCount
 {850100969u,AtomType::Float32,0,1,1,false}, // manifold.normal.x
 {992054370u,AtomType::Float32,0,1,1,false}, // cachedRotationB.s
 {1037122550u,AtomType::Unsigned,0,1,1,false}, // cache.count
 {1058809389u,AtomType::Float32,0,1,1,false}, // cachedRelativePose.p.x
 {1063525372u,AtomType::Float32,0,1,1,false}, // manifold.points[1].normalVelocity
 {1111753356u,AtomType::Unsigned,0,1,1,false}, // cache.indexB[0]
 {1136595888u,AtomType::Float32,0,1,1,false}, // cachedRelativePose.q.s
 {1157758377u,AtomType::Float32,0,1,1,false}, // cachedRelativePose.p.y
 {1163755328u,AtomType::Unsigned,0,1,1,false}, // cache.indexA[2]
 {1189211251u,AtomType::Float32,0,1,1,false}, // manifold.points[0].normalImpulse
 {1205291112u,AtomType::Float32,0,1,1,false}, // manifold.points[0].anchorB.x
 {1232920347u,AtomType::Float32,0,1,1,false}, // manifold.points[1].normalImpulse
 {1391466534u,AtomType::Float32,0,1,1,false}, // invMassA
 {1420680097u,AtomType::Float32,0,1,1,false}, // rollingResistance
 {1572431943u,AtomType::Reference,4102,1,1,false}, // contactId
 {1618704423u,AtomType::Float32,0,1,1,false}, // manifold.points[0].anchorB.y
 {1660191808u,AtomType::Float32,0,1,1,false}, // manifold.points[1].anchorB.x
 {1902514535u,AtomType::Reference,4103,1,1,false}, // shapeIdB
 {1919970927u,AtomType::Unsigned,0,1,1,false}, // cache.indexB[2]
 {1924615496u,AtomType::Unsigned,0,1,1,false}, // simFlags
 {1964022884u,AtomType::Float32,0,1,1,false}, // manifold.points[1].separation
 {2308023698u,AtomType::Float32,0,1,1,false}, // manifold.points[0].anchorA.y
 {2338188230u,AtomType::Float32,0,1,1,false}, // manifold.points[1].anchorA.x
 {2370397421u,AtomType::Float32,0,1,1,false}, // manifold.points[1].tangentImpulse
 {2384423763u,AtomType::Float32,0,1,1,false}, // manifold.points[0].anchorA.x
 {2593724195u,AtomType::Float32,0,1,1,false}, // tangentSpeed
 {2664133291u,AtomType::Float32,0,1,1,false}, // manifold.points[1].anchorA.y
 {2683644512u,AtomType::Float32,0,1,1,false}, // manifold.rollingImpulse
 {2772055957u,AtomType::Reference,4103,1,1,false}, // shapeIdA
 {2796387850u,AtomType::Float32,0,1,1,false}, // cachedRotationB.c
 {2890898297u,AtomType::Unsigned,0,1,1,false}, // manifold.points[0].id
 {2941529568u,AtomType::Reference,4097,1,1,true}, // bodyIdA
 {2960685639u,AtomType::Float32,0,1,1,false}, // manifold.normal.y
 {2963952968u,AtomType::Unsigned,0,1,1,false}, // cache.indexA[0]
 {2969646725u,AtomType::Unsigned,0,1,1,false}, // cache.indexA[1]
 {2986050957u,AtomType::Boolean,0,1,1,false}, // manifold.points[0].persisted
 {3033444301u,AtomType::Float32,0,1,1,false}, // cachedRelativePose.q.c
 {3123532566u,AtomType::Reference,4097,1,1,true}, // bodyIdB
 {3259252967u,AtomType::Float32,0,1,1,false}, // invMassB
 {3291251533u,AtomType::Unsigned,0,1,1,false}, // manifold.points[1].id
 {3404790181u,AtomType::Unsigned,0,1,1,false}, // cache.indexB[1]
 {3498112136u,AtomType::Boolean,0,1,1,false}, // manifold.points[1].persisted
 {3671577127u,AtomType::Float32,0,1,1,false}, // invIA
 {3676319181u,AtomType::Float32,0,1,1,false}, // manifold.points[1].totalNormalImpulse
 {3696697204u,AtomType::Float32,0,1,1,false}, // cachedRotationA.s
 {3724314271u,AtomType::Float32,0,1,1,false}, // manifold.points[1].baseSeparation
 {3793782805u,AtomType::Float32,0,1,1,false}, // manifold.points[0].totalNormalImpulse
 {4016109257u,AtomType::Float32,0,1,1,false}, // restitution
 {4075390407u,AtomType::Float32,0,1,1,false}, // manifold.points[0].tangentImpulse
 {4122760065u,AtomType::Float32,0,1,1,false}, // manifold.points[0].normalVelocity
}};
float value(const Atom &a)noexcept{return std::bit_cast<float>(std::uint32_t(a.bits));}
Status validate(const Record &record)noexcept{
 if(record.identity.kind!=contact_sim_kind || !record.identity.simulation || !record.identity.generation || record.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=record.fields[i];if(f.id!=d.id || f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){bool absent=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;
 if(a.bits || (absent?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else{if(a.identity.kind||a.identity.simulation||a.identity.generation)return fail(Error::InvalidArgument);
 if(d.type==AtomType::Float32 && (a.bits>UINT32_MAX||!std::isfinite(value(a))))return fail(Error::NonCanonical);if(d.type==AtomType::Boolean&&a.bits>1)return fail(Error::NonCanonical);}}
 return {};
}
}
std::span<const FieldSpec> contact_sim_fields()noexcept{return definitions;}
ContactSimImage::ContactSimImage()noexcept{for(size_t i=0;i<fields.size();++i)fields[i]={definitions[i].id,std::span(atoms).subspan(i,1)};record.fields=fields;}
Status capture_contact_sim(const SpContactSim &native,Identity identity,const ContactMappings &maps,ContactSimImage &image)noexcept{
 if(identity.kind!=contact_sim_kind || !identity.simulation || !identity.generation || native.manifold.pointCount<0||native.manifold.pointCount>2||native.cache.count>3)return fail(Error::InvalidArgument);
 ContactSimImage staged;
 if(native.bodySimIndexA < -1)return fail(Error::InvalidArgument);if(native.bodySimIndexA==-1){}else{auto id=maps.solver_body.canonical(std::uint32_t(native.bodySimIndexA));if(!id)return fail(id.error());staged.atoms[0].identity=*id;}
 staged.atoms[1].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].separation);
 staged.atoms[2].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].baseSeparation);
 staged.atoms[3].bits=std::bit_cast<std::uint32_t>(native.friction);
 if(native.bodySimIndexB < -1)return fail(Error::InvalidArgument);if(native.bodySimIndexB==-1){}else{auto id=maps.solver_body.canonical(std::uint32_t(native.bodySimIndexB));if(!id)return fail(id.error());staged.atoms[4].identity=*id;}
 staged.atoms[5].bits=std::bit_cast<std::uint32_t>(native.cachedRotationA.c);
 staged.atoms[6].bits=std::bit_cast<std::uint32_t>(native.invIB);
 staged.atoms[7].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].anchorB.y);
 staged.atoms[8].bits=native.manifold.pointCount;
 staged.atoms[9].bits=std::bit_cast<std::uint32_t>(native.manifold.normal.x);
 staged.atoms[10].bits=std::bit_cast<std::uint32_t>(native.cachedRotationB.s);
 staged.atoms[11].bits=native.cache.count;
 staged.atoms[12].bits=std::bit_cast<std::uint32_t>(native.cachedRelativePose.p.x);
 staged.atoms[13].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].normalVelocity);
 staged.atoms[14].bits=native.cache.indexB[0];
 staged.atoms[15].bits=std::bit_cast<std::uint32_t>(native.cachedRelativePose.q.s);
 staged.atoms[16].bits=std::bit_cast<std::uint32_t>(native.cachedRelativePose.p.y);
 staged.atoms[17].bits=native.cache.indexA[2];
 staged.atoms[18].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].normalImpulse);
 staged.atoms[19].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].anchorB.x);
 staged.atoms[20].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].normalImpulse);
 staged.atoms[21].bits=std::bit_cast<std::uint32_t>(native.invMassA);
 staged.atoms[22].bits=std::bit_cast<std::uint32_t>(native.rollingResistance);
 if(native.contactId < 0)return fail(Error::InvalidArgument);{auto id=maps.contact.canonical(std::uint32_t(native.contactId));if(!id)return fail(id.error());staged.atoms[23].identity=*id;}
 staged.atoms[24].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].anchorB.y);
 staged.atoms[25].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].anchorB.x);
 if(native.shapeIdB < 0)return fail(Error::InvalidArgument);{auto id=maps.shape.canonical(std::uint32_t(native.shapeIdB));if(!id)return fail(id.error());staged.atoms[26].identity=*id;}
 staged.atoms[27].bits=native.cache.indexB[2];
 staged.atoms[28].bits=native.simFlags;
 staged.atoms[29].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].separation);
 staged.atoms[30].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].anchorA.y);
 staged.atoms[31].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].anchorA.x);
 staged.atoms[32].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].tangentImpulse);
 staged.atoms[33].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].anchorA.x);
 staged.atoms[34].bits=std::bit_cast<std::uint32_t>(native.tangentSpeed);
 staged.atoms[35].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].anchorA.y);
 staged.atoms[36].bits=std::bit_cast<std::uint32_t>(native.manifold.rollingImpulse);
 if(native.shapeIdA < 0)return fail(Error::InvalidArgument);{auto id=maps.shape.canonical(std::uint32_t(native.shapeIdA));if(!id)return fail(id.error());staged.atoms[37].identity=*id;}
 staged.atoms[38].bits=std::bit_cast<std::uint32_t>(native.cachedRotationB.c);
 staged.atoms[39].bits=native.manifold.points[0].id;
 if(native.bodyIdA < -1)return fail(Error::InvalidArgument);if(native.bodyIdA==-1){}else{auto id=maps.body.canonical(std::uint32_t(native.bodyIdA));if(!id)return fail(id.error());staged.atoms[40].identity=*id;}
 staged.atoms[41].bits=std::bit_cast<std::uint32_t>(native.manifold.normal.y);
 staged.atoms[42].bits=native.cache.indexA[0];
 staged.atoms[43].bits=native.cache.indexA[1];
 staged.atoms[44].bits=native.manifold.points[0].persisted;
 staged.atoms[45].bits=std::bit_cast<std::uint32_t>(native.cachedRelativePose.q.c);
 if(native.bodyIdB < -1)return fail(Error::InvalidArgument);if(native.bodyIdB==-1){}else{auto id=maps.body.canonical(std::uint32_t(native.bodyIdB));if(!id)return fail(id.error());staged.atoms[46].identity=*id;}
 staged.atoms[47].bits=std::bit_cast<std::uint32_t>(native.invMassB);
 staged.atoms[48].bits=native.manifold.points[1].id;
 staged.atoms[49].bits=native.cache.indexB[1];
 staged.atoms[50].bits=native.manifold.points[1].persisted;
 staged.atoms[51].bits=std::bit_cast<std::uint32_t>(native.invIA);
 staged.atoms[52].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].totalNormalImpulse);
 staged.atoms[53].bits=std::bit_cast<std::uint32_t>(native.cachedRotationA.s);
 staged.atoms[54].bits=std::bit_cast<std::uint32_t>(native.manifold.points[1].baseSeparation);
 staged.atoms[55].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].totalNormalImpulse);
 staged.atoms[56].bits=std::bit_cast<std::uint32_t>(native.restitution);
 staged.atoms[57].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].tangentImpulse);
 staged.atoms[58].bits=std::bit_cast<std::uint32_t>(native.manifold.points[0].normalVelocity);
 staged.record.identity=identity;if(auto v=validate(staged.record);!v)return v;image.atoms=staged.atoms;image.record.identity=identity;return {}; }
Status restore_contact_sim(const Record &record,const ContactMappings &maps,SpContactSim &destination)noexcept{if(auto v=validate(record);!v)return v;SpContactSim staged={};
 {const auto &id=record.fields[0].atoms[0].identity;if(!id.kind)staged.bodySimIndexA=-1;else{auto slot=maps.solver_body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodySimIndexA=int(*slot);}}
 staged.manifold.points[0].separation=value(record.fields[1].atoms[0]);
 staged.manifold.points[0].baseSeparation=value(record.fields[2].atoms[0]);
 staged.friction=value(record.fields[3].atoms[0]);
 {const auto &id=record.fields[4].atoms[0].identity;if(!id.kind)staged.bodySimIndexB=-1;else{auto slot=maps.solver_body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodySimIndexB=int(*slot);}}
 staged.cachedRotationA.c=value(record.fields[5].atoms[0]);
 staged.invIB=value(record.fields[6].atoms[0]);
 staged.manifold.points[1].anchorB.y=value(record.fields[7].atoms[0]);
 if(record.fields[8].atoms[0].bits>2147483647ULL)return fail(Error::Overflow);staged.manifold.pointCount=static_cast<std::remove_reference_t<decltype(staged.manifold.pointCount)>>(record.fields[8].atoms[0].bits);
 staged.manifold.normal.x=value(record.fields[9].atoms[0]);
 staged.cachedRotationB.s=value(record.fields[10].atoms[0]);
 if(record.fields[11].atoms[0].bits>65535ULL)return fail(Error::Overflow);staged.cache.count=static_cast<std::remove_reference_t<decltype(staged.cache.count)>>(record.fields[11].atoms[0].bits);
 staged.cachedRelativePose.p.x=value(record.fields[12].atoms[0]);
 staged.manifold.points[1].normalVelocity=value(record.fields[13].atoms[0]);
 if(record.fields[14].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexB[0]=static_cast<std::remove_reference_t<decltype(staged.cache.indexB[0])>>(record.fields[14].atoms[0].bits);
 staged.cachedRelativePose.q.s=value(record.fields[15].atoms[0]);
 staged.cachedRelativePose.p.y=value(record.fields[16].atoms[0]);
 if(record.fields[17].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexA[2]=static_cast<std::remove_reference_t<decltype(staged.cache.indexA[2])>>(record.fields[17].atoms[0].bits);
 staged.manifold.points[0].normalImpulse=value(record.fields[18].atoms[0]);
 staged.manifold.points[0].anchorB.x=value(record.fields[19].atoms[0]);
 staged.manifold.points[1].normalImpulse=value(record.fields[20].atoms[0]);
 staged.invMassA=value(record.fields[21].atoms[0]);
 staged.rollingResistance=value(record.fields[22].atoms[0]);
 {const auto &id=record.fields[23].atoms[0].identity;if(!id.kind)staged.contactId=-1;else{auto slot=maps.contact.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.contactId=int(*slot);}}
 staged.manifold.points[0].anchorB.y=value(record.fields[24].atoms[0]);
 staged.manifold.points[1].anchorB.x=value(record.fields[25].atoms[0]);
 {const auto &id=record.fields[26].atoms[0].identity;if(!id.kind)staged.shapeIdB=-1;else{auto slot=maps.shape.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.shapeIdB=int(*slot);}}
 if(record.fields[27].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexB[2]=static_cast<std::remove_reference_t<decltype(staged.cache.indexB[2])>>(record.fields[27].atoms[0].bits);
 if(record.fields[28].atoms[0].bits>4294967295ULL)return fail(Error::Overflow);staged.simFlags=static_cast<std::remove_reference_t<decltype(staged.simFlags)>>(record.fields[28].atoms[0].bits);
 staged.manifold.points[1].separation=value(record.fields[29].atoms[0]);
 staged.manifold.points[0].anchorA.y=value(record.fields[30].atoms[0]);
 staged.manifold.points[1].anchorA.x=value(record.fields[31].atoms[0]);
 staged.manifold.points[1].tangentImpulse=value(record.fields[32].atoms[0]);
 staged.manifold.points[0].anchorA.x=value(record.fields[33].atoms[0]);
 staged.tangentSpeed=value(record.fields[34].atoms[0]);
 staged.manifold.points[1].anchorA.y=value(record.fields[35].atoms[0]);
 staged.manifold.rollingImpulse=value(record.fields[36].atoms[0]);
 {const auto &id=record.fields[37].atoms[0].identity;if(!id.kind)staged.shapeIdA=-1;else{auto slot=maps.shape.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.shapeIdA=int(*slot);}}
 staged.cachedRotationB.c=value(record.fields[38].atoms[0]);
 if(record.fields[39].atoms[0].bits>65535ULL)return fail(Error::Overflow);staged.manifold.points[0].id=static_cast<std::remove_reference_t<decltype(staged.manifold.points[0].id)>>(record.fields[39].atoms[0].bits);
 {const auto &id=record.fields[40].atoms[0].identity;if(!id.kind)staged.bodyIdA=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyIdA=int(*slot);}}
 staged.manifold.normal.y=value(record.fields[41].atoms[0]);
 if(record.fields[42].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexA[0]=static_cast<std::remove_reference_t<decltype(staged.cache.indexA[0])>>(record.fields[42].atoms[0].bits);
 if(record.fields[43].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexA[1]=static_cast<std::remove_reference_t<decltype(staged.cache.indexA[1])>>(record.fields[43].atoms[0].bits);
 if(record.fields[44].atoms[0].bits>1ULL)return fail(Error::Overflow);staged.manifold.points[0].persisted=static_cast<std::remove_reference_t<decltype(staged.manifold.points[0].persisted)>>(record.fields[44].atoms[0].bits);
 staged.cachedRelativePose.q.c=value(record.fields[45].atoms[0]);
 {const auto &id=record.fields[46].atoms[0].identity;if(!id.kind)staged.bodyIdB=-1;else{auto slot=maps.body.native(id);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyIdB=int(*slot);}}
 staged.invMassB=value(record.fields[47].atoms[0]);
 if(record.fields[48].atoms[0].bits>65535ULL)return fail(Error::Overflow);staged.manifold.points[1].id=static_cast<std::remove_reference_t<decltype(staged.manifold.points[1].id)>>(record.fields[48].atoms[0].bits);
 if(record.fields[49].atoms[0].bits>255ULL)return fail(Error::Overflow);staged.cache.indexB[1]=static_cast<std::remove_reference_t<decltype(staged.cache.indexB[1])>>(record.fields[49].atoms[0].bits);
 if(record.fields[50].atoms[0].bits>1ULL)return fail(Error::Overflow);staged.manifold.points[1].persisted=static_cast<std::remove_reference_t<decltype(staged.manifold.points[1].persisted)>>(record.fields[50].atoms[0].bits);
 staged.invIA=value(record.fields[51].atoms[0]);
 staged.manifold.points[1].totalNormalImpulse=value(record.fields[52].atoms[0]);
 staged.cachedRotationA.s=value(record.fields[53].atoms[0]);
 staged.manifold.points[1].baseSeparation=value(record.fields[54].atoms[0]);
 staged.manifold.points[0].totalNormalImpulse=value(record.fields[55].atoms[0]);
 staged.restitution=value(record.fields[56].atoms[0]);
 staged.manifold.points[0].tangentImpulse=value(record.fields[57].atoms[0]);
 staged.manifold.points[0].normalVelocity=value(record.fields[58].atoms[0]);
 if(staged.manifold.pointCount>2||staged.cache.count>3)return fail(Error::InvalidArgument);destination=staged;return {}; }
}
