// SPDX-License-Identifier: MIT
#include "owned_solver_bodies.hpp"
#include <algorithm>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr size_t columns[5]={2,11,0,6,9},capacities[5]={10,7,5,3,1};
struct Layout {size_t bytes{};bool valid=true;template<class T>size_t add(size_t n)noexcept{if(!valid||n>SIZE_MAX/sizeof(T)){valid=false;return 0;}size_t pad=(alignof(T)-bytes%alignof(T))%alignof(T);if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*sizeof(T)){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*sizeof(T);return offset;}};
template<class T>std::span<T>take(void*p,Layout&l,size_t n)noexcept{size_t offset=l.add<T>(n);auto*q=reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);for(size_t i=0;i<n;++i)std::construct_at(q+i);return {q,n};}
bool nil(Identity i)noexcept{return !i.kind&&!i.simulation&&!i.generation;}
const Record*find(std::span<const Record>rs,Identity id)noexcept{auto i=std::lower_bound(rs.begin(),rs.end(),id.simulation,[](const Record&r,uint64_t x){return r.identity.simulation<x;});return i!=rs.end()&&i->identity==id?&*i:nullptr;}
bool sorted(std::span<const Record>rs,uint32_t kind)noexcept{if(rs.size()>100000)return false;for(size_t i=0;i<rs.size();++i)if(rs[i].identity.kind!=kind||!rs[i].identity.simulation||!rs[i].identity.generation||(i&&rs[i-1].identity.simulation>=rs[i].identity.simulation))return false;return true;}
bool membership_shape(const Record&r)noexcept{
 auto ds=solver_membership_fields();if(r.fields.size()!=ds.size())return false;
 for(size_t i=0;i<ds.size();++i){const auto&f=r.fields[i];const auto&d=ds[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return false;
 for(auto a:f.atoms)if(d.type==AtomType::Reference){if(a.bits||a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)return false;}else if(!nil(a.identity)||a.bits>100000)return false;}
 return r.fields[4].atoms[0].bits<=3&&r.fields[8].atoms[0].identity==r.identity;
}
size_t count(const Record&r,size_t c)noexcept{return r.fields[columns[c]].atoms.size();}
size_t capacity(const Record&r,size_t c)noexcept{return size_t(r.fields[capacities[c]].atoms[0].bits);}
}
OwnedSolverBodies::OwnedSolverBodies(OwnedSolverBodies&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),sets_(std::exchange(o.sets_,std::span<OwnedSetBodies>{})){if(o.pools_){pools_.emplace(std::move(*o.pools_));o.pools_.reset();}}
OwnedSolverBodies::~OwnedSolverBodies(){if(block_)allocator_->deallocate(block_);}
Result<OwnedSolverBodies>restore_owned_solver_bodies(const std::array<PoolCandidateInput,SP_POOL_COUNT>&input,SolverBodyRecords records,Allocator&allocator)noexcept{
 if(!sorted(records.sets,solver_set_kind)||!sorted(records.bodies,bodysim_kind)||!sorted(records.states,bodystate_kind))return fail(Error::InvalidArgument);
 const size_t slot_count=input[SP_POOL_SET].occupants.size();if(slot_count<3||slot_count>100000)return fail(Error::InvalidArgument);
 std::array<size_t,5>sums{};size_t max_count=0;
 for(const auto&r:records.sets){if(!membership_shape(r))return fail(Error::IncompatibleSchema);for(size_t c=0;c<5;++c){if(count(r,c)>capacity(r,c)||capacity(r,c)>100000-sums[c])return fail(Error::CapacityExceeded);sums[c]+=capacity(r,c);max_count=std::max(max_count,count(r,c));}}
 Layout layout;layout.add<uint32_t>(slot_count);layout.add<OwnedSetBodies>(slot_count);layout.add<Identity>(max_count);
 for(const auto&pool:input){if(pool.occupants.size()>100000)return fail(Error::CapacityExceeded);layout.add<uint8_t>(pool.occupants.size());}
 for(const auto&r:records.sets){for(size_t c:{size_t(0),size_t(2),size_t(3),size_t(4)})layout.add<int>(capacity(r,c));layout.add<b2BodySim>(capacity(r,0));layout.add<b2BodyState>(capacity(r,1));}
 if(!layout.valid)return fail(Error::CapacityExceeded);
 OwnedSolverBodies out;out.allocator_=&allocator;out.bytes_=layout.bytes;out.block_=allocator.allocate(layout.bytes,std::max({alignof(std::max_align_t),alignof(b2BodySim),alignof(b2BodyState)}),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout cursor;auto placement=take<uint32_t>(out.block_,cursor,slot_count);out.sets_=take<OwnedSetBodies>(out.block_,cursor,slot_count);for(auto&e:out.sets_)e.membership.set_index=-1;/* free set slots stay explicitly unassigned */auto scratch=take<Identity>(out.block_,cursor,max_count);std::array<std::span<uint8_t>,SP_POOL_COUNT> marks;for(size_t p=0;p<SP_POOL_COUNT;++p)marks[p]=take<uint8_t>(out.block_,cursor,input[p].occupants.size());
 bool roles[3]{};size_t live_sets=0;uint32_t next=3;
 for(size_t i=0;i<slot_count;++i){const auto&r=input[SP_POOL_SET].occupants[i];if(r.fields.size()!=2||r.fields[0].atoms.size()!=1)return fail(Error::InvalidArgument);auto id=r.fields[0].atoms[0].identity;
 if(nil(id)){placement[i]=UINT32_MAX;continue;}const Record*member=find(records.sets,id);if(!member)return fail(Error::StaleGeneration);++live_sets;uint32_t role=uint32_t(member->fields[4].atoms[0].bits);if(role<3){if(roles[role])return fail(Error::InvalidArgument);roles[role]=true;placement[i]=role;}else placement[i]=UINT32_MAX;}
 if(live_sets!=records.sets.size()||!roles[0]||!roles[1]||!roles[2])return fail(Error::InvalidArgument);
 for(auto&target:placement)if(target==UINT32_MAX)target=next++;
 std::array<std::span<const uint32_t>,SP_POOL_COUNT>placements{};placements[SP_POOL_SET]=placement;auto pools=restore_world_pool_candidates_placed(input,placements,allocator);if(!pools)return fail(pools.error());out.pools_.emplace(std::move(*pools));
 const auto&set_map=out.pools().pool(SP_POOL_SET)->object_map();const auto&body_map=out.pools().pool(SP_POOL_BODY)->object_map();const auto&joint_map=out.pools().pool(SP_POOL_JOINT)->object_map();const auto&contact_map=out.pools().pool(SP_POOL_CONTACT)->object_map();const auto&island_map=out.pools().pool(SP_POOL_ISLAND)->object_map();SolverMembershipMaps maps{set_map,body_map,joint_map,contact_map,island_map};
 size_t bodies_restored=0,states_restored=0;
 for(const auto&r:records.sets){auto destination=set_map.native(r.identity);if(!destination)return fail(destination.error());auto&entry=out.sets_[*destination];auto&m=entry.membership;
 auto ids0=take<int>(out.block_,cursor,capacity(r,0)),ids2=take<int>(out.block_,cursor,capacity(r,2)),ids3=take<int>(out.block_,cursor,capacity(r,3)),ids4=take<int>(out.block_,cursor,capacity(r,4));entry.bodies=take<b2BodySim>(out.block_,cursor,capacity(r,0));entry.states=take<b2BodyState>(out.block_,cursor,capacity(r,1));
 m={int(*destination),ids0.data(),ids2.data(),ids3.data(),ids4.data(),0,uint32_t(ids0.size()),0,uint32_t(entry.states.size()),0,uint32_t(ids2.size()),0,uint32_t(ids3.size()),0,uint32_t(ids4.size())};
 if(auto s=restore_solver_membership(r,maps,scratch,m);!s)return fail(s.error());
 const int*arrays[4]={m.body_ids,m.joint_ids,m.contact_ids,m.island_ids};const size_t lengths[4]={m.body_count,m.joint_count,m.contact_count,m.island_count};constexpr SpWorldPool pools_for[4]={SP_POOL_BODY,SP_POOL_JOINT,SP_POOL_CONTACT,SP_POOL_ISLAND};
 for(size_t c=0;c<4;++c)for(size_t i=0;i<lengths[c];++i){auto n=uint32_t(arrays[c][i]);auto&mark=marks[pools_for[c]];if(n>=mark.size()||mark[n])return fail(Error::InvalidArgument);mark[n]=1;}
 for(size_t i=0;i<m.body_count;++i){Identity id=r.fields[columns[0]].atoms[i].identity;const Record*payload=find(records.bodies,id);if(!payload)return fail(Error::StaleGeneration);if(auto s=restore_body_sim(*payload,body_map,entry.bodies[i]);!s)return fail(s.error());if(entry.bodies[i].bodyId!=m.body_ids[i])return fail(Error::StaleGeneration);++bodies_restored;
 if(i<m.state_count){id.kind=bodystate_kind;const Record*state=find(records.states,id);if(!state)return fail(Error::StaleGeneration);if(auto s=restore_body_state(*state,entry.states[i]);!s)return fail(s.error());++states_restored;}}
 }
 if(cursor.bytes!=layout.bytes||bodies_restored!=records.bodies.size()||states_restored!=records.states.size())return fail(Error::InvalidArgument);
 const auto&body_pool=out.pools().pool(SP_POOL_BODY)->pool();if(bodies_restored!=body_pool.allocated_count-body_pool.free_count)return fail(Error::InvalidArgument);
 return out;
}
}
