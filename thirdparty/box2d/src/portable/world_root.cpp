// SPDX-License-Identifier: MIT
#include "world_root.hpp"
#include "runtime_scratch_bridge.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
enum Slot : uint8_t {RestitutionThreshold,EnableContinuous,RestitutionCallback,ContactSpeed,PreSolveFcn,Gravity,ContactDampingRatio,FrictionCallback,EnableContactSoftening,InvH,MaxCapacity,EnableSleep,HitEventThreshold,CustomFilterContext,StepIndex,EnableWarmStarting,InvDt,PreSolveContext,MaxLinearSpeed,CustomFilterFcn,UserData,EndEventArrayIndex,EnableSpeculative,ContactRecycleDistance,SplitIslandId,ContactHertz};
// Field IDs are the audited b2World ledger identities, sorted ascending.
constexpr std::array<FieldSpec,26> schema{{
 {72699401u,AtomType::Float32,0,1,1,false},{239712017u,AtomType::Boolean,0,1,1,false},{365379194u,AtomType::Reference,binding_kind,1,1,true},{456005333u,AtomType::Float32,0,1,1,false},
 {632059963u,AtomType::Reference,binding_kind,1,1,true},{637021331u,AtomType::Float32,0,2,2,false},{702194073u,AtomType::Float32,0,1,1,false},{740753763u,AtomType::Reference,binding_kind,1,1,true},
 {990647451u,AtomType::Boolean,0,1,1,false},{1127606435u,AtomType::Float32,0,1,1,false},{1187328238u,AtomType::Unsigned,0,5,5,false},{1467084152u,AtomType::Boolean,0,1,1,false},
 {1503109477u,AtomType::Float32,0,1,1,false},{1744770253u,AtomType::Reference,binding_kind,1,1,true},{2036259224u,AtomType::Unsigned,0,1,1,false},{2047878478u,AtomType::Boolean,0,1,1,false},
 {2275713676u,AtomType::Float32,0,1,1,false},{2518667865u,AtomType::Reference,binding_kind,1,1,true},{2690654282u,AtomType::Float32,0,1,1,false},{2880244965u,AtomType::Reference,binding_kind,1,1,true},
 {2962704077u,AtomType::Reference,binding_kind,1,1,true},{3117329116u,AtomType::Unsigned,0,1,1,false},{3271035987u,AtomType::Boolean,0,1,1,false},{3323819933u,AtomType::Float32,0,1,1,false},
 {3395882216u,AtomType::Reference,island_kind,1,1,true},{3712950962u,AtomType::Float32,0,1,1,false}}};
bool nil(Identity i)noexcept{return !i.kind&&!i.simulation&&!i.generation;}
float value(const Atom&a)noexcept{return std::bit_cast<float>(uint32_t(a.bits));}
Status validate(const Record&r)noexcept{
 if(r.identity.kind!=world_root_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=schema.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<schema.size();++i){const auto&f=r.fields[i];const auto&d=schema[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);
  for(const auto&a:f.atoms)switch(d.type){
   case AtomType::Float32:if(!nil(a.identity)||a.bits>UINT32_MAX||!std::isfinite(value(a)))return fail(Error::InvalidArgument);break;
   case AtomType::Boolean:if(!nil(a.identity)||a.bits>1)return fail(Error::InvalidArgument);break;
   case AtomType::Unsigned:if(!nil(a.identity)||(i==MaxCapacity&&a.bits>uint64_t(INT32_MAX))||(i==EndEventArrayIndex&&a.bits>1))return fail(Error::InvalidArgument);break;
   case AtomType::Reference:if(a.bits||(nil(a.identity)?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);break;
   default:return fail(Error::IncompatibleSchema);}}
 return {};
}
}
WorldRootImage::WorldRootImage()noexcept{}
std::span<const FieldSpec> world_root_fields()noexcept{return schema;}
Status capture_world_root(const SpWorldRoot&r,Identity identity,const IdentityMap&island,const PointerIdentityMap&bindings,WorldRootImage&out)noexcept{
 if(identity.kind!=world_root_kind||!identity.simulation||!identity.generation)return fail(Error::InvalidArgument);
 std::array<Atom,31> atoms{};std::array<std::pair<size_t,size_t>,26> ranges{};size_t c=0;bool finite=true;
 auto begin=[&](Slot s){ranges[s].first=c;};auto end=[&](Slot s){ranges[s].second=c-ranges[s].first;};
 auto flt=[&](Slot s,float v){begin(s);finite=finite&&std::isfinite(v);atoms[c++].bits=std::bit_cast<uint32_t>(v);end(s);};
 auto flag=[&](Slot s,bool v){begin(s);atoms[c++].bits=v?1:0;end(s);};
 auto ref=[&](Slot s,void*p)->Status{begin(s);auto id=bindings.canonical(p);if(!id)return fail(id.error());atoms[c++].identity=*id;end(s);return {};};
 for(int s=0;s<26;++s)switch(Slot(s)){
  case RestitutionThreshold:flt(Slot(s),r.restitutionThreshold);break;case EnableContinuous:flag(Slot(s),r.enableContinuous);break;
  case RestitutionCallback:if(auto v=ref(Slot(s),r.restitutionCallback);!v)return v;break;case ContactSpeed:flt(Slot(s),r.contactSpeed);break;
  case PreSolveFcn:if(auto v=ref(Slot(s),r.preSolveFcn);!v)return v;break;
  case Gravity:begin(Slot(s));finite=finite&&std::isfinite(r.gravity[0])&&std::isfinite(r.gravity[1]);atoms[c++].bits=std::bit_cast<uint32_t>(r.gravity[0]);atoms[c++].bits=std::bit_cast<uint32_t>(r.gravity[1]);end(Slot(s));break;
  case ContactDampingRatio:flt(Slot(s),r.contactDampingRatio);break;case FrictionCallback:if(auto v=ref(Slot(s),r.frictionCallback);!v)return v;break;
  case EnableContactSoftening:flag(Slot(s),r.enableContactSoftening);break;case InvH:flt(Slot(s),r.inv_h);break;
  case MaxCapacity:begin(Slot(s));for(int k=0;k<5;++k){if(r.maxCapacity[k]<0)return fail(Error::InvalidArgument);atoms[c++].bits=uint64_t(r.maxCapacity[k]);}end(Slot(s));break;
  case EnableSleep:flag(Slot(s),r.enableSleep);break;case HitEventThreshold:flt(Slot(s),r.hitEventThreshold);break;
  case CustomFilterContext:if(auto v=ref(Slot(s),r.customFilterContext);!v)return v;break;
  case StepIndex:begin(Slot(s));atoms[c++].bits=r.stepIndex;end(Slot(s));break;case EnableWarmStarting:flag(Slot(s),r.enableWarmStarting);break;
  case InvDt:flt(Slot(s),r.inv_dt);break;case PreSolveContext:if(auto v=ref(Slot(s),r.preSolveContext);!v)return v;break;
  case MaxLinearSpeed:flt(Slot(s),r.maxLinearSpeed);break;case CustomFilterFcn:if(auto v=ref(Slot(s),r.customFilterFcn);!v)return v;break;
  case UserData:if(auto v=ref(Slot(s),r.userData);!v)return v;break;
  case EndEventArrayIndex:if(r.endEventArrayIndex<0||r.endEventArrayIndex>1)return fail(Error::InvalidArgument);begin(Slot(s));atoms[c++].bits=uint64_t(r.endEventArrayIndex);end(Slot(s));break;
  case EnableSpeculative:flag(Slot(s),r.enableSpeculative);break;case ContactRecycleDistance:flt(Slot(s),r.contactRecycleDistance);break;
  case SplitIslandId:begin(Slot(s));if(r.splitIslandId<-1)return fail(Error::InvalidArgument);if(r.splitIslandId>=0){auto id=island.canonical(uint32_t(r.splitIslandId));if(!id)return fail(id.error());atoms[c].identity=*id;}++c;end(Slot(s));break;
  case ContactHertz:flt(Slot(s),r.contactHertz);break;}
 if(!finite||c!=atoms.size())return fail(Error::InvalidArgument);
 out.atoms=atoms;for(size_t i=0;i<schema.size();++i)out.fields[i]={schema[i].id,std::span<const Atom>(out.atoms).subspan(ranges[i].first,ranges[i].second)};
 out.record={identity,out.fields};return validate(out.record);
}
Status restore_world_root(const Record&record,const IdentityMap&island,const PointerIdentityMap&bindings,SpWorldRoot&destination)noexcept{
 if(auto v=validate(record);!v)return v;SpWorldRoot r{};const auto&f=record.fields;
 auto one=[&](Slot s)->const Atom&{return f[s].atoms[0];};
 auto ptr=[&](Slot s,void*&out)->Status{auto p=bindings.native(one(s).identity);if(!p)return fail(p.error());out=*p;return {};};
 r.restitutionThreshold=value(one(RestitutionThreshold));r.enableContinuous=one(EnableContinuous).bits!=0;r.contactSpeed=value(one(ContactSpeed));r.gravity[0]=value(f[Gravity].atoms[0]);r.gravity[1]=value(f[Gravity].atoms[1]);
 r.contactDampingRatio=value(one(ContactDampingRatio));r.enableContactSoftening=one(EnableContactSoftening).bits!=0;r.inv_h=value(one(InvH));for(int k=0;k<5;++k)r.maxCapacity[k]=int(f[MaxCapacity].atoms[k].bits);
 r.enableSleep=one(EnableSleep).bits!=0;r.hitEventThreshold=value(one(HitEventThreshold));r.stepIndex=one(StepIndex).bits;r.enableWarmStarting=one(EnableWarmStarting).bits!=0;r.inv_dt=value(one(InvDt));
 r.maxLinearSpeed=value(one(MaxLinearSpeed));r.endEventArrayIndex=int(one(EndEventArrayIndex).bits);r.enableSpeculative=one(EnableSpeculative).bits!=0;r.contactRecycleDistance=value(one(ContactRecycleDistance));r.contactHertz=value(one(ContactHertz));
 r.splitIslandId=-1;if(!nil(one(SplitIslandId).identity)){auto n=island.native(one(SplitIslandId).identity);if(!n)return fail(n.error());if(*n>uint32_t(INT32_MAX))return fail(Error::Overflow);r.splitIslandId=int(*n);}
 for(auto[s,p]:{std::pair{RestitutionCallback,&r.restitutionCallback},std::pair{PreSolveFcn,&r.preSolveFcn},std::pair{FrictionCallback,&r.frictionCallback},std::pair{CustomFilterContext,&r.customFilterContext},std::pair{PreSolveContext,&r.preSolveContext},std::pair{CustomFilterFcn,&r.customFilterFcn},std::pair{UserData,&r.userData}})if(auto v=ptr(s,*p);!v)return v;
 destination=r;return {};
}
OwnedWorldRoot::OwnedWorldRoot(OwnedWorldRoot&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),world_(std::exchange(o.world_,nullptr)){if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedWorldRoot::~OwnedWorldRoot(){if(block_)allocator_->deallocate(block_);}
Result<OwnedWorldRoot>restore_owned_world_root(OwnedSolverWorld&&prior,const Record&root,const PointerIdentityMap&bindings,Allocator&allocator)noexcept{
 if(!prior.has_storage())return fail(Error::InvalidArgument);
 const auto&pools=prior.sets().constraints().graph().islands().solver_bodies().pools();
 SpWorldRoot scalars{};if(auto s=restore_world_root(root,pools.pool(SP_POOL_ISLAND)->object_map(),bindings,scalars);!s)return fail(s.error());
 const auto layout=spWorldRootLayout();if(!layout.size||!layout.alignment)return fail(Error::IncompatibleSchema);
 OwnedWorldRoot out;out.allocator_=&allocator;out.bytes_=layout.size;out.block_=allocator.allocate(layout.size,std::max(alignof(std::max_align_t),size_t(layout.alignment)),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);out.world_=out.block_;
 const auto&v=prior.ownership_view();
 SpWorldRootArrays arrays{const_cast<void*>(v.bodies),const_cast<void*>(v.contacts),const_cast<void*>(v.joints),const_cast<void*>(v.islands),const_cast<void*>(v.sets),v.graph,v.body_count,v.contact_count,v.joint_count,v.island_count,v.set_count,
  pools.pool(SP_POOL_BODY)->pool(),pools.pool(SP_POOL_CONTACT)->pool(),pools.pool(SP_POOL_JOINT)->pool(),pools.pool(SP_POOL_ISLAND)->pool(),pools.pool(SP_POOL_SET)->pool()};
 if(!spAssembleWorldRoot(out.world_,&arrays)||!spImportWorldRoot(&scalars,out.world_)||!spValidateNormalizedRuntimeScratch(out.world_))return fail(Error::InvalidArgument);
 SpWorldOwnershipView rv{};if(!spWorldRootOwnershipView(out.world_,&rv)||rv.bodies!=v.bodies||rv.sets!=v.sets||!spValidateWorldOwnership(&rv)||!spValidateWorldRootPools(out.world_))return fail(Error::InvalidArgument);
 out.prior_.emplace(std::move(prior));return out;
}
}
