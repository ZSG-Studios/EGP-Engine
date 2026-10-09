// SPDX-License-Identifier: MIT
#include "shape_codec.hpp"
#include <bit>
#include <cmath>
#include <climits>
namespace superpos::box2d_portable {namespace {using namespace canonical;
constexpr std::array<FieldSpec,42> defs{{
 {172967969u,AtomType::Float32,0,1,1,false}, // localCentroid.y
 {308532798u,AtomType::Reference,capsule_geometry_kind,1,1,true}, // capsule
 {788929384u,AtomType::Reference,shape_kind,1,1,true}, // prevShapeId
 {863213289u,AtomType::Boolean,0,1,1,false}, // enableSensorEvents
 {873403256u,AtomType::Unsigned,0,1,1,false}, // material.userMaterialId
 {943573530u,AtomType::Reference,sensor_kind,1,1,true}, // sensorIndex
 {1103474092u,AtomType::Boolean,0,1,1,false}, // enablePreSolveEvents
 {1110156954u,AtomType::Boolean,0,1,1,false}, // enlargedAABB
 {1314449289u,AtomType::Reference,chain_segment_geometry_kind,1,1,true}, // chainSegment
 {1418584960u,AtomType::Reference,polygon_geometry_kind,1,1,true}, // polygon
 {1608858041u,AtomType::Float32,0,1,1,false}, // material.tangentSpeed
 {2045310765u,AtomType::Unsigned,0,1,1,false}, // filter.maskBits
 {2071901541u,AtomType::Float32,0,1,1,false}, // aabb.lowerBound.y
 {2125815636u,AtomType::Boolean,0,1,1,false}, // enableCustomFiltering
 {2289342784u,AtomType::Float32,0,1,1,false}, // fatAABB.lowerBound.y
 {2340407634u,AtomType::Signed,0,1,1,false}, // filter.groupIndex
 {2345401782u,AtomType::Float32,0,1,1,false}, // material.rollingResistance
 {2345567800u,AtomType::Boolean,0,1,1,false}, // enableContactEvents
 {2370162819u,AtomType::Float32,0,1,1,false}, // localCentroid.x
 {2378658154u,AtomType::Reference,shape_kind,1,1,false}, // id
 {2620068510u,AtomType::Reference,segment_geometry_kind,1,1,true}, // segment
 {2687267670u,AtomType::Reference,shape_kind,1,1,true}, // nextShapeId
 {2756101973u,AtomType::Float32,0,1,1,false}, // aabbMargin
 {3026951593u,AtomType::Unsigned,0,1,1,false}, // material.customColor
 {3046558805u,AtomType::Boolean,0,1,1,false}, // enableHitEvents
 {3066987613u,AtomType::Unsigned,0,1,1,false}, // type
 {3273544198u,AtomType::Float32,0,1,1,false}, // aabb.lowerBound.x
 {3293986057u,AtomType::Float32,0,1,1,false}, // material.restitution
 {3374513007u,AtomType::Float32,0,1,1,false}, // fatAABB.upperBound.y
 {3386715070u,AtomType::Float32,0,1,1,false}, // aabb.upperBound.x
 {3434865332u,AtomType::Float32,0,1,1,false}, // density
 {3503895113u,AtomType::Float32,0,1,1,false}, // fatAABB.upperBound.x
 {3600340570u,AtomType::Reference,body_kind,1,1,false}, // bodyId
 {3687494932u,AtomType::Unsigned,0,1,1,false}, // proxyKey.type
 {3730222909u,AtomType::Unsigned,0,1,1,false}, // filter.categoryBits
 {3753438895u,AtomType::Reference,circle_geometry_kind,1,1,true}, // circle
 {3782958091u,AtomType::Unsigned,0,1,1,false}, // generation
 {3800752586u,AtomType::Reference,shape_proxy_kind,1,1,true}, // proxyKey.reference
 {3828633403u,AtomType::Float32,0,1,1,false}, // aabb.upperBound.y
 {4053162842u,AtomType::Float32,0,1,1,false}, // fatAABB.lowerBound.x
 {4155372449u,AtomType::Float32,0,1,1,false}, // material.friction
 {4159879901u,AtomType::Reference,binding_kind,1,1,true}, // userData
}};
Status valid(const Record &r)noexcept{if(r.identity.kind!=shape_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=defs.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<defs.size();++i){auto &f=r.fields[i];auto &d=defs[i];if(f.id!=d.id||f.atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=f.atoms[0];
 if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}
 else if(a.identity.kind||a.identity.simulation||a.identity.generation)return fail(Error::InvalidArgument);
 else if(d.type==AtomType::Boolean&&a.bits>1)return fail(Error::NonCanonical);
 else if(d.type==AtomType::Float32&&(a.bits>UINT32_MAX||!std::isfinite(std::bit_cast<float>(uint32_t(a.bits)))))return fail(Error::NonCanonical);}
 return {};
}
Result<int> local(const IdentityMap &map,Identity id)noexcept{auto n=map.native(id);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);return int(*n);}
template<class T>void copy_image(const T &from,T &to)noexcept{to.atoms=from.atoms;for(size_t i=0;i<to.fields.size();++i){auto offset=from.fields[i].atoms.data()-from.atoms.data();to.fields[i].atoms=std::span(to.atoms).subspan(size_t(offset),from.fields[i].atoms.size());}to.record.identity=from.record.identity;}
}
std::span<const FieldSpec> shape_fields()noexcept{return defs;}
ShapeImage::ShapeImage()noexcept{for(size_t i=0;i<42;++i)fields[i]={defs[i].id,std::span(atoms).subspan(i,1)};base.fields=fields;}
Status restore_shape(const Record &r,const Record &payload,const ShapeMappings &maps,b2Shape &out)noexcept{if(auto s=valid(r);!s)return s;b2Shape staged{};
 staged.localCentroid.y=std::bit_cast<float>(uint32_t(r.fields[0].atoms[0].bits));
 if(r.fields[2].atoms[0].identity.kind){auto n=local(maps.shape,r.fields[2].atoms[0].identity);if(!n)return fail(n.error());staged.prevShapeId=*n;}else staged.prevShapeId=-1;
 staged.enableSensorEvents=r.fields[3].atoms[0].bits!=0;
 staged.material.userMaterialId=r.fields[4].atoms[0].bits;
 if(r.fields[5].atoms[0].identity.kind){auto n=local(maps.sensor,r.fields[5].atoms[0].identity);if(!n)return fail(n.error());staged.sensorIndex=*n;}else staged.sensorIndex=-1;
 staged.enablePreSolveEvents=r.fields[6].atoms[0].bits!=0;
 staged.enlargedAABB=r.fields[7].atoms[0].bits!=0;
 staged.material.tangentSpeed=std::bit_cast<float>(uint32_t(r.fields[10].atoms[0].bits));
 staged.filter.maskBits=r.fields[11].atoms[0].bits;
 staged.aabb.lowerBound.y=std::bit_cast<float>(uint32_t(r.fields[12].atoms[0].bits));
 staged.enableCustomFiltering=r.fields[13].atoms[0].bits!=0;
 staged.fatAABB.lowerBound.y=std::bit_cast<float>(uint32_t(r.fields[14].atoms[0].bits));
 {auto n=std::bit_cast<int64_t>(r.fields[15].atoms[0].bits);if(n<INT_MIN||n>INT_MAX)return fail(Error::InvalidArgument);staged.filter.groupIndex=int(n);}
 staged.material.rollingResistance=std::bit_cast<float>(uint32_t(r.fields[16].atoms[0].bits));
 if(staged.material.rollingResistance<0)return fail(Error::InvalidArgument);
 staged.enableContactEvents=r.fields[17].atoms[0].bits!=0;
 staged.localCentroid.x=std::bit_cast<float>(uint32_t(r.fields[18].atoms[0].bits));
 if(r.fields[19].atoms[0].identity.kind){auto n=local(maps.shape,r.fields[19].atoms[0].identity);if(!n)return fail(n.error());staged.id=*n;}else staged.id=-1;
 if(r.fields[21].atoms[0].identity.kind){auto n=local(maps.shape,r.fields[21].atoms[0].identity);if(!n)return fail(n.error());staged.nextShapeId=*n;}else staged.nextShapeId=-1;
 staged.aabbMargin=std::bit_cast<float>(uint32_t(r.fields[22].atoms[0].bits));
 if(staged.aabbMargin<0)return fail(Error::InvalidArgument);
 if(r.fields[23].atoms[0].bits>UINT32_MAX)return fail(Error::InvalidArgument);staged.material.customColor=uint32_t(r.fields[23].atoms[0].bits);
 staged.enableHitEvents=r.fields[24].atoms[0].bits!=0;
 if(r.fields[25].atoms[0].bits>=b2_shapeTypeCount)return fail(Error::InvalidArgument);staged.type=static_cast<b2ShapeType>(r.fields[25].atoms[0].bits);
 staged.aabb.lowerBound.x=std::bit_cast<float>(uint32_t(r.fields[26].atoms[0].bits));
 staged.material.restitution=std::bit_cast<float>(uint32_t(r.fields[27].atoms[0].bits));
 if(staged.material.restitution<0)return fail(Error::InvalidArgument);
 staged.fatAABB.upperBound.y=std::bit_cast<float>(uint32_t(r.fields[28].atoms[0].bits));
 staged.aabb.upperBound.x=std::bit_cast<float>(uint32_t(r.fields[29].atoms[0].bits));
 staged.density=std::bit_cast<float>(uint32_t(r.fields[30].atoms[0].bits));
 if(staged.density<0)return fail(Error::InvalidArgument);
 staged.fatAABB.upperBound.x=std::bit_cast<float>(uint32_t(r.fields[31].atoms[0].bits));
 if(r.fields[32].atoms[0].identity.kind){auto n=local(maps.body,r.fields[32].atoms[0].identity);if(!n)return fail(n.error());staged.bodyId=*n;}else staged.bodyId=-1;
 staged.filter.categoryBits=r.fields[34].atoms[0].bits;
 if(!r.fields[36].atoms[0].bits||r.fields[36].atoms[0].bits>UINT16_MAX)return fail(Error::InvalidArgument);staged.generation=uint16_t(r.fields[36].atoms[0].bits);
 staged.aabb.upperBound.y=std::bit_cast<float>(uint32_t(r.fields[38].atoms[0].bits));
 staged.fatAABB.lowerBound.x=std::bit_cast<float>(uint32_t(r.fields[39].atoms[0].bits));
 staged.material.friction=std::bit_cast<float>(uint32_t(r.fields[40].atoms[0].bits));
 if(staged.material.friction<0)return fail(Error::InvalidArgument);
 {auto p=maps.binding.native(r.fields[41].atoms[0].identity);if(!p)return fail(p.error());staged.userData=*p;}
 if(r.fields[19].atoms[0].identity!=r.identity)return fail(Error::StaleGeneration);
 if(staged.aabb.lowerBound.x>staged.aabb.upperBound.x||staged.aabb.lowerBound.y>staged.aabb.upperBound.y||staged.fatAABB.lowerBound.x>staged.fatAABB.upperBound.x||staged.fatAABB.lowerBound.y>staged.fatAABB.upperBound.y)return fail(Error::InvalidArgument);
 const auto proxy=r.fields[37].atoms[0].identity;auto proxy_type=r.fields[33].atoms[0].bits;if(proxy.kind){if(proxy_type>=3||!maps.proxies[proxy_type])return fail(Error::InvalidArgument);auto n=local(*maps.proxies[proxy_type],proxy);if(!n||*n>(INT_MAX>>2))return fail(Error::InvalidArgument);staged.proxyKey=(*n<<2)|int(proxy_type);}else{if(proxy_type)return fail(Error::NonCanonical);staged.proxyKey=-1;}
 if(staged.type==0){if(r.fields[35].atoms[0].identity!=payload.identity||payload.identity.simulation!=r.identity.simulation||payload.identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);}else if(r.fields[35].atoms[0].identity.kind)return fail(Error::IncompatibleSchema);
 if(staged.type==1){if(r.fields[1].atoms[0].identity!=payload.identity||payload.identity.simulation!=r.identity.simulation||payload.identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);}else if(r.fields[1].atoms[0].identity.kind)return fail(Error::IncompatibleSchema);
 if(staged.type==2){if(r.fields[20].atoms[0].identity!=payload.identity||payload.identity.simulation!=r.identity.simulation||payload.identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);}else if(r.fields[20].atoms[0].identity.kind)return fail(Error::IncompatibleSchema);
 if(staged.type==3){if(r.fields[9].atoms[0].identity!=payload.identity||payload.identity.simulation!=r.identity.simulation||payload.identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);}else if(r.fields[9].atoms[0].identity.kind)return fail(Error::IncompatibleSchema);
 if(staged.type==4){if(r.fields[8].atoms[0].identity!=payload.identity||payload.identity.simulation!=r.identity.simulation||payload.identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);}else if(r.fields[8].atoms[0].identity.kind)return fail(Error::IncompatibleSchema);
 switch(staged.type){
 case 0:{auto s=restore_circle_geometry(payload,staged.circle);if(!s)return s;break;}
 case 1:{auto s=restore_capsule_geometry(payload,staged.capsule);if(!s)return s;break;}
 case 2:{auto s=restore_segment_geometry(payload,staged.segment);if(!s)return s;break;}
 case 3:{auto s=restore_polygon_geometry(payload,staged.polygon);if(!s)return s;break;}
 case 4:{auto s=restore_chain_segment_geometry(payload,maps.chain,staged.chainSegment);if(!s)return s;break;}
 default:return fail(Error::IncompatibleSchema);}
 out=staged;return {}; }
Status capture_shape(const b2Shape &native,Identity id,const ShapeMappings &maps,ShapeImage &out)noexcept{ShapeImage staged;staged.base.identity=id;if(native.type<0||native.type>=b2_shapeTypeCount)return fail(Error::InvalidArgument);
 staged.atoms[0].bits=std::bit_cast<uint32_t>(native.localCentroid.y);
 if(native.prevShapeId<-1)return fail(Error::InvalidArgument);if(native.prevShapeId!=-1){auto n=maps.shape.canonical(uint32_t(native.prevShapeId));if(!n||n->kind!=shape_kind)return fail(Error::StaleGeneration);staged.atoms[2].identity=*n;}
 staged.atoms[3].bits=uint64_t(native.enableSensorEvents);
 staged.atoms[4].bits=uint64_t(native.material.userMaterialId);
 if(native.sensorIndex<-1)return fail(Error::InvalidArgument);if(native.sensorIndex!=-1){auto n=maps.sensor.canonical(uint32_t(native.sensorIndex));if(!n||n->kind!=sensor_kind)return fail(Error::StaleGeneration);staged.atoms[5].identity=*n;}
 staged.atoms[6].bits=uint64_t(native.enablePreSolveEvents);
 staged.atoms[7].bits=uint64_t(native.enlargedAABB);
 staged.atoms[10].bits=std::bit_cast<uint32_t>(native.material.tangentSpeed);
 staged.atoms[11].bits=uint64_t(native.filter.maskBits);
 staged.atoms[12].bits=std::bit_cast<uint32_t>(native.aabb.lowerBound.y);
 staged.atoms[13].bits=uint64_t(native.enableCustomFiltering);
 staged.atoms[14].bits=std::bit_cast<uint32_t>(native.fatAABB.lowerBound.y);
 staged.atoms[15].bits=std::bit_cast<uint64_t>(int64_t(native.filter.groupIndex));
 staged.atoms[16].bits=std::bit_cast<uint32_t>(native.material.rollingResistance);
 staged.atoms[17].bits=uint64_t(native.enableContactEvents);
 staged.atoms[18].bits=std::bit_cast<uint32_t>(native.localCentroid.x);
 if(native.id<0)return fail(Error::InvalidArgument);{auto n=maps.shape.canonical(uint32_t(native.id));if(!n||n->kind!=shape_kind)return fail(Error::StaleGeneration);staged.atoms[19].identity=*n;}
 if(native.nextShapeId<-1)return fail(Error::InvalidArgument);if(native.nextShapeId!=-1){auto n=maps.shape.canonical(uint32_t(native.nextShapeId));if(!n||n->kind!=shape_kind)return fail(Error::StaleGeneration);staged.atoms[21].identity=*n;}
 staged.atoms[22].bits=std::bit_cast<uint32_t>(native.aabbMargin);
 staged.atoms[23].bits=uint64_t(native.material.customColor);
 staged.atoms[24].bits=uint64_t(native.enableHitEvents);
 staged.atoms[25].bits=uint64_t(native.type);
 staged.atoms[26].bits=std::bit_cast<uint32_t>(native.aabb.lowerBound.x);
 staged.atoms[27].bits=std::bit_cast<uint32_t>(native.material.restitution);
 staged.atoms[28].bits=std::bit_cast<uint32_t>(native.fatAABB.upperBound.y);
 staged.atoms[29].bits=std::bit_cast<uint32_t>(native.aabb.upperBound.x);
 staged.atoms[30].bits=std::bit_cast<uint32_t>(native.density);
 staged.atoms[31].bits=std::bit_cast<uint32_t>(native.fatAABB.upperBound.x);
 if(native.bodyId<0)return fail(Error::InvalidArgument);{auto n=maps.body.canonical(uint32_t(native.bodyId));if(!n||n->kind!=body_kind)return fail(Error::StaleGeneration);staged.atoms[32].identity=*n;}
 staged.atoms[34].bits=uint64_t(native.filter.categoryBits);
 staged.atoms[36].bits=uint64_t(native.generation);
 staged.atoms[38].bits=std::bit_cast<uint32_t>(native.aabb.upperBound.y);
 staged.atoms[39].bits=std::bit_cast<uint32_t>(native.fatAABB.lowerBound.x);
 staged.atoms[40].bits=std::bit_cast<uint32_t>(native.material.friction);
 {auto n=maps.binding.canonical(native.userData);if(!n)return fail(n.error());staged.atoms[41].identity=*n;}
 if(native.proxyKey< -1)return fail(Error::InvalidArgument);if(native.proxyKey!=-1){unsigned type=unsigned(native.proxyKey)&3;if(type>=3||!maps.proxies[type])return fail(Error::InvalidArgument);auto ref=maps.proxies[type]->canonical(uint32_t(native.proxyKey)>>2);if(!ref||ref->kind!=shape_proxy_kind)return fail(Error::StaleGeneration);staged.atoms[37].identity=*ref;staged.atoms[33].bits=type;}
 Record payload;switch(native.type){
 case 0:{Identity child{circle_geometry_kind,id.simulation,id.generation};auto s=capture_circle_geometry(native.circle,child,staged.circle);if(!s)return s;payload=staged.circle.record;staged.atoms[35].identity=child;break;}
 case 1:{Identity child{capsule_geometry_kind,id.simulation,id.generation};auto s=capture_capsule_geometry(native.capsule,child,staged.capsule);if(!s)return s;payload=staged.capsule.record;staged.atoms[1].identity=child;break;}
 case 2:{Identity child{segment_geometry_kind,id.simulation,id.generation};auto s=capture_segment_geometry(native.segment,child,staged.segment);if(!s)return s;payload=staged.segment.record;staged.atoms[20].identity=child;break;}
 case 3:{Identity child{polygon_geometry_kind,id.simulation,id.generation};auto s=capture_polygon_geometry(native.polygon,child,staged.polygon);if(!s)return s;payload=staged.polygon.record;staged.atoms[9].identity=child;break;}
 case 4:{Identity child{chain_segment_geometry_kind,id.simulation,id.generation};auto s=capture_chain_segment_geometry(native.chainSegment,child,maps.chain,staged.chain_segment);if(!s)return s;payload=staged.chain_segment.record;staged.atoms[8].identity=child;break;}
 default:return fail(Error::IncompatibleSchema);}
 b2Shape checked{};if(auto s=restore_shape(staged.base,payload,maps,checked);!s)return s;
 out.atoms=staged.atoms;out.base.identity=id;out.records[0]=out.base;switch(native.type){
 case 0:copy_image(staged.circle,out.circle);out.records[1]=out.circle.record;break;
 case 1:copy_image(staged.capsule,out.capsule);out.records[1]=out.capsule.record;break;
 case 2:copy_image(staged.segment,out.segment);out.records[1]=out.segment.record;break;
 case 3:copy_image(staged.polygon,out.polygon);out.records[1]=out.polygon.record;break;
 case 4:copy_image(staged.chain_segment,out.chain_segment);out.records[1]=out.chain_segment.record;break;
 default:return fail(Error::IncompatibleSchema);}
 return {}; }
}
