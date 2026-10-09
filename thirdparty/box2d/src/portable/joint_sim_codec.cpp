// SPDX-License-Identifier: MIT
#include "joint_sim_codec.hpp"
#include <bit>
#include <cmath>
namespace superpos::box2d_portable {namespace {using namespace canonical;
constexpr std::array<FieldSpec,29> definitions{{
 {27591916u,AtomType::Float32,0,1,1,false}, // constraintSoftness.impulseScale
 {144431924u,AtomType::Float32,0,1,1,false}, // constraintHertz
 {447603880u,AtomType::Float32,0,1,1,false}, // forceThreshold
 {606917327u,AtomType::Float32,0,1,1,false}, // localFrameA.q.s
 {680370190u,AtomType::Reference,4356,1,1,true}, // payload.weldJoint
 {812338650u,AtomType::Reference,4352,1,1,true}, // payload.distanceJoint
 {819984866u,AtomType::Float32,0,1,1,false}, // localFrameA.p.x
 {925894140u,AtomType::Reference,4357,1,1,true}, // payload.wheelJoint
 {1045719429u,AtomType::Float32,0,1,1,false}, // constraintDampingRatio
 {1140596134u,AtomType::Float32,0,1,1,false}, // constraintSoftness.biasRate
 {1194479335u,AtomType::Float32,0,1,1,false}, // localFrameA.q.c
 {1483098992u,AtomType::Float32,0,1,1,false}, // invMassB
 {1508358016u,AtomType::Float32,0,1,1,false}, // invIA
 {2125157284u,AtomType::Reference,4097,1,1,false}, // bodyIdA
 {2252524786u,AtomType::Unsigned,0,1,1,false}, // type
 {2392725875u,AtomType::Float32,0,1,1,false}, // invMassA
 {2710962651u,AtomType::Float32,0,1,1,false}, // localFrameB.p.y
 {2787077259u,AtomType::Reference,4353,1,1,true}, // payload.motorJoint
 {2819135347u,AtomType::Float32,0,1,1,false}, // constraintSoftness.massScale
 {2997792868u,AtomType::Float32,0,1,1,false}, // localFrameA.p.y
 {3101388093u,AtomType::Float32,0,1,1,false}, // localFrameB.q.s
 {3102192311u,AtomType::Float32,0,1,1,false}, // torqueThreshold
 {3192983307u,AtomType::Reference,4104,1,1,false}, // jointId
 {3209339088u,AtomType::Float32,0,1,1,false}, // localFrameB.q.c
 {3288570776u,AtomType::Reference,4355,1,1,true}, // payload.revoluteJoint
 {3739244201u,AtomType::Reference,4354,1,1,true}, // payload.prismaticJoint
 {3911195583u,AtomType::Reference,4097,1,1,false}, // bodyIdB
 {4093164627u,AtomType::Float32,0,1,1,false}, // localFrameB.p.x
 {4218649110u,AtomType::Float32,0,1,1,false}, // invIB
}};
float value(const Atom &a)noexcept{return std::bit_cast<float>(std::uint32_t(a.bits));}
Status validate(const Record &r)noexcept{
 if(r.identity.kind!=joint_sim_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=definitions.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<definitions.size();++i){const auto &d=definitions[i];const auto &f=r.fields[i];if(f.id!=d.id||f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else{if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>UINT32_MAX)return fail(Error::InvalidArgument);if(d.type==AtomType::Float32&&!std::isfinite(value(a)))return fail(Error::NonCanonical);}}
 return {};
}
}
std::span<const FieldSpec> joint_sim_fields()noexcept{return definitions;}
JointSimImage::JointSimImage()noexcept{for(size_t i=0;i<fields.size();++i)fields[i]={definitions[i].id,std::span(atoms).subspan(i,1)};base.fields=fields;}
Status capture_joint_sim(const SpJointSim &native,Identity identity,const JointSimMappings &maps,JointSimImage &image)noexcept{
 if(native.type<0||native.type>6)return fail(Error::Unsupported);JointSimImage staged;
 staged.atoms[0].bits=std::bit_cast<std::uint32_t>(native.constraintSoftness.impulseScale);
 staged.atoms[1].bits=std::bit_cast<std::uint32_t>(native.constraintHertz);
 staged.atoms[2].bits=std::bit_cast<std::uint32_t>(native.forceThreshold);
 staged.atoms[3].bits=std::bit_cast<std::uint32_t>(native.localFrameA.q.s);
 if(native.type==5)staged.atoms[4].identity={weld_joint_kind,identity.simulation,identity.generation};
 if(native.type==0)staged.atoms[5].identity={distance_joint_kind,identity.simulation,identity.generation};
 staged.atoms[6].bits=std::bit_cast<std::uint32_t>(native.localFrameA.p.x);
 if(native.type==6)staged.atoms[7].identity={wheel_joint_kind,identity.simulation,identity.generation};
 staged.atoms[8].bits=std::bit_cast<std::uint32_t>(native.constraintDampingRatio);
 staged.atoms[9].bits=std::bit_cast<std::uint32_t>(native.constraintSoftness.biasRate);
 staged.atoms[10].bits=std::bit_cast<std::uint32_t>(native.localFrameA.q.c);
 staged.atoms[11].bits=std::bit_cast<std::uint32_t>(native.invMassB);
 staged.atoms[12].bits=std::bit_cast<std::uint32_t>(native.invIA);
 if(native.bodyIdA<0)return fail(Error::InvalidArgument);{auto id=maps.body.canonical(std::uint32_t(native.bodyIdA));if(!id)return fail(id.error());staged.atoms[13].identity=*id;}
 staged.atoms[14].bits=std::uint32_t(native.type);
 staged.atoms[15].bits=std::bit_cast<std::uint32_t>(native.invMassA);
 staged.atoms[16].bits=std::bit_cast<std::uint32_t>(native.localFrameB.p.y);
 if(native.type==2)staged.atoms[17].identity={motor_joint_kind,identity.simulation,identity.generation};
 staged.atoms[18].bits=std::bit_cast<std::uint32_t>(native.constraintSoftness.massScale);
 staged.atoms[19].bits=std::bit_cast<std::uint32_t>(native.localFrameA.p.y);
 staged.atoms[20].bits=std::bit_cast<std::uint32_t>(native.localFrameB.q.s);
 staged.atoms[21].bits=std::bit_cast<std::uint32_t>(native.torqueThreshold);
 if(native.jointId<0)return fail(Error::InvalidArgument);{auto id=maps.joint.canonical(std::uint32_t(native.jointId));if(!id)return fail(id.error());staged.atoms[22].identity=*id;}
 staged.atoms[23].bits=std::bit_cast<std::uint32_t>(native.localFrameB.q.c);
 if(native.type==4)staged.atoms[24].identity={revolute_joint_kind,identity.simulation,identity.generation};
 if(native.type==3)staged.atoms[25].identity={prismatic_joint_kind,identity.simulation,identity.generation};
 if(native.bodyIdB<0)return fail(Error::InvalidArgument);{auto id=maps.body.canonical(std::uint32_t(native.bodyIdB));if(!id)return fail(id.error());staged.atoms[26].identity=*id;}
 staged.atoms[27].bits=std::bit_cast<std::uint32_t>(native.localFrameB.p.x);
 staged.atoms[28].bits=std::bit_cast<std::uint32_t>(native.invIB);
 if(staged.atoms[22].identity.simulation!=identity.simulation||staged.atoms[22].identity.generation!=identity.generation)return fail(Error::StaleGeneration);staged.base.identity=identity;if(auto v=validate(staged.base);!v)return v;
 switch(native.type){
 case 0:if(auto v=capture_distance_joint(native.distanceJoint,{distance_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.distanceJoint);!v)return v;break;
 case 2:if(auto v=capture_motor_joint(native.motorJoint,{motor_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.motorJoint);!v)return v;break;
 case 3:if(auto v=capture_prismatic_joint(native.prismaticJoint,{prismatic_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.prismaticJoint);!v)return v;break;
 case 4:if(auto v=capture_revolute_joint(native.revoluteJoint,{revolute_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.revoluteJoint);!v)return v;break;
 case 5:if(auto v=capture_weld_joint(native.weldJoint,{weld_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.weldJoint);!v)return v;break;
 case 6:if(auto v=capture_wheel_joint(native.wheelJoint,{wheel_joint_kind,identity.simulation,identity.generation},maps.solver_body,staged.wheelJoint);!v)return v;break;
 case 1:break;default:return fail(Error::Unsupported);}
 image.atoms=staged.atoms;image.base.identity=identity;image.records[0]=image.base;image.count=1;
 switch(native.type){
 case 0:image.distanceJoint.atoms=staged.distanceJoint.atoms;image.distanceJoint.record.identity=staged.distanceJoint.record.identity;image.records[1]=image.distanceJoint.record;image.count=2;break;
 case 2:image.motorJoint.atoms=staged.motorJoint.atoms;image.motorJoint.record.identity=staged.motorJoint.record.identity;image.records[1]=image.motorJoint.record;image.count=2;break;
 case 3:image.prismaticJoint.atoms=staged.prismaticJoint.atoms;image.prismaticJoint.record.identity=staged.prismaticJoint.record.identity;image.records[1]=image.prismaticJoint.record;image.count=2;break;
 case 4:image.revoluteJoint.atoms=staged.revoluteJoint.atoms;image.revoluteJoint.record.identity=staged.revoluteJoint.record.identity;image.records[1]=image.revoluteJoint.record;image.count=2;break;
 case 5:image.weldJoint.atoms=staged.weldJoint.atoms;image.weldJoint.record.identity=staged.weldJoint.record.identity;image.records[1]=image.weldJoint.record;image.count=2;break;
 case 6:image.wheelJoint.atoms=staged.wheelJoint.atoms;image.wheelJoint.record.identity=staged.wheelJoint.record.identity;image.records[1]=image.wheelJoint.record;image.count=2;break;
 case 1:break;default:return fail(Error::Unsupported);}return {}; }
Status restore_joint_sim(const Record &record,const Record *payload,const JointSimMappings &maps,SpJointSim &destination)noexcept{if(auto v=validate(record);!v)return v;SpJointSim staged={};
 staged.constraintSoftness.impulseScale=value(record.fields[0].atoms[0]);
 staged.constraintHertz=value(record.fields[1].atoms[0]);
 staged.forceThreshold=value(record.fields[2].atoms[0]);
 staged.localFrameA.q.s=value(record.fields[3].atoms[0]);
 staged.localFrameA.p.x=value(record.fields[6].atoms[0]);
 staged.constraintDampingRatio=value(record.fields[8].atoms[0]);
 staged.constraintSoftness.biasRate=value(record.fields[9].atoms[0]);
 staged.localFrameA.q.c=value(record.fields[10].atoms[0]);
 staged.invMassB=value(record.fields[11].atoms[0]);
 staged.invIA=value(record.fields[12].atoms[0]);
 {auto slot=maps.body.native(record.fields[13].atoms[0].identity);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyIdA=int(*slot);}
 if(record.fields[14].atoms[0].bits>6)return fail(Error::Unsupported);staged.type=static_cast<b2JointType>(record.fields[14].atoms[0].bits);
 staged.invMassA=value(record.fields[15].atoms[0]);
 staged.localFrameB.p.y=value(record.fields[16].atoms[0]);
 staged.constraintSoftness.massScale=value(record.fields[18].atoms[0]);
 staged.localFrameA.p.y=value(record.fields[19].atoms[0]);
 staged.localFrameB.q.s=value(record.fields[20].atoms[0]);
 staged.torqueThreshold=value(record.fields[21].atoms[0]);
 {auto slot=maps.joint.native(record.fields[22].atoms[0].identity);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.jointId=int(*slot);}
 staged.localFrameB.q.c=value(record.fields[23].atoms[0]);
 {auto slot=maps.body.native(record.fields[26].atoms[0].identity);if(!slot)return fail(slot.error());if(*slot>INT32_MAX)return fail(Error::Overflow);staged.bodyIdB=int(*slot);}
 staged.localFrameB.p.x=value(record.fields[27].atoms[0]);
 staged.invIB=value(record.fields[28].atoms[0]);
 if(record.fields[22].atoms[0].identity.simulation!=record.identity.simulation||record.fields[22].atoms[0].identity.generation!=record.identity.generation)return fail(Error::StaleGeneration);
 {const auto &id=record.fields[4].atoms[0].identity;if(staged.type==5){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 {const auto &id=record.fields[5].atoms[0].identity;if(staged.type==0){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 {const auto &id=record.fields[7].atoms[0].identity;if(staged.type==6){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 {const auto &id=record.fields[17].atoms[0].identity;if(staged.type==2){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 {const auto &id=record.fields[24].atoms[0].identity;if(staged.type==4){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 {const auto &id=record.fields[25].atoms[0].identity;if(staged.type==3){if(!payload||payload->identity!=id||id.simulation!=record.identity.simulation||id.generation!=record.identity.generation)return fail(Error::IncompatibleSchema);}else if(id.kind)return fail(Error::IncompatibleSchema);}
 if(staged.type==1&&payload)return fail(Error::IncompatibleSchema);switch(staged.type){
 case 0:if(auto v=restore_distance_joint(*payload,maps.solver_body,staged.distanceJoint);!v)return v;break;
 case 2:if(auto v=restore_motor_joint(*payload,maps.solver_body,staged.motorJoint);!v)return v;break;
 case 3:if(auto v=restore_prismatic_joint(*payload,maps.solver_body,staged.prismaticJoint);!v)return v;break;
 case 4:if(auto v=restore_revolute_joint(*payload,maps.solver_body,staged.revoluteJoint);!v)return v;break;
 case 5:if(auto v=restore_weld_joint(*payload,maps.solver_body,staged.weldJoint);!v)return v;break;
 case 6:if(auto v=restore_wheel_joint(*payload,maps.solver_body,staged.wheelJoint);!v)return v;break;
 case 1:break;default:return fail(Error::Unsupported);}destination=staged;return {}; }
}
