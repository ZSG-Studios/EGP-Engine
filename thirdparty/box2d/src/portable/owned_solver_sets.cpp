// SPDX-License-Identifier: MIT
#include "owned_solver_sets.hpp"
#include <algorithm>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}template<class T>size_t add(size_t n)noexcept{return raw(n,sizeof(T),alignof(T));}};
void*raw_take(void*p,Layout&l,size_t n,size_t s,size_t a)noexcept{return static_cast<std::byte*>(p)+l.raw(n,s,a);}
template<class T>std::span<T>take(void*p,Layout&l,size_t n)noexcept{auto*q=reinterpret_cast<T*>(static_cast<std::byte*>(p)+l.add<T>(n));for(size_t i=0;i<n;++i)std::construct_at(q+i);return {q,n};}
bool same_ids(const int*a,const int*b,uint32_t n)noexcept{return n==0||std::equal(a,a+n,b);}
}
OwnedSolverSets::OwnedSolverSets(OwnedSolverSets&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),sets_(std::exchange(o.sets_,nullptr)),count_(std::exchange(o.count_,0)),stride_(o.stride_){if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedSolverSets::~OwnedSolverSets(){if(block_)allocator_->deallocate(block_);}
Result<OwnedSolverSets>assemble_owned_solver_sets(OwnedSolverConstraints&&prior,Allocator&allocator)noexcept{
 if(!prior.has_storage())return fail(Error::InvalidArgument);
 const auto bodies=prior.graph().islands().solver_bodies().sets();const auto constraints=prior.sets();if(bodies.size()!=constraints.size()||bodies.size()<3||bodies.size()>100000)return fail(Error::InvalidArgument);
 const auto native=spNativeSolverSetLayout();if(!native.size||!native.island_size)return fail(Error::IncompatibleSchema);
 size_t islands=0,widest=0;for(const auto&s:bodies){const auto&m=s.membership;if(m.island_capacity>100000-islands)return fail(Error::CapacityExceeded);islands+=m.island_capacity;widest=std::max({widest,size_t(m.body_capacity),size_t(m.joint_capacity),size_t(m.contact_capacity),size_t(m.island_capacity)});}
 Layout plan;plan.raw(bodies.size(),native.size,native.alignment);plan.raw(islands,native.island_size,native.island_alignment);plan.add<int>(4*widest);if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedSolverSets out;out.allocator_=&allocator;out.bytes_=std::max(plan.bytes,size_t(1));out.block_=allocator.allocate(out.bytes_,std::max({alignof(std::max_align_t),size_t(native.alignment),size_t(native.island_alignment)}),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout cursor;out.sets_=raw_take(out.block_,cursor,bodies.size(),native.size,native.alignment);out.count_=uint32_t(bodies.size());out.stride_=native.size;auto*island_storage=static_cast<std::byte*>(raw_take(out.block_,cursor,islands,native.island_size,native.island_alignment));auto scratch=take<int>(out.block_,cursor,4*widest);if(cursor.bytes!=plan.bytes)return fail(Error::InvalidArgument);
 size_t island_cursor=0;
 for(uint32_t slot=0;slot<out.count_;++slot){const auto&owned=bodies[slot];const auto&m=owned.membership;const auto&c=constraints[slot];void*set=const_cast<void*>(out.native_set(slot));SpOwnedSetArrays a{};
  if(m.set_index>=0){if(m.set_index!=int(slot)||!c||c->set_index()!=int(slot)||owned.bodies.size()!=m.body_capacity||owned.states.size()!=m.state_capacity||c->contact_count()!=m.contact_count||c->contact_capacity()!=m.contact_capacity||c->joint_count()!=m.joint_count||c->joint_capacity()!=m.joint_capacity)return fail(Error::InvalidArgument);
   a={owned.bodies.data(),m.body_count,m.body_capacity,owned.states.data(),m.state_count,m.state_capacity,const_cast<void*>(c->native_joint_array()),m.joint_count,m.joint_capacity,const_cast<void*>(c->native_contact_array()),m.contact_count,m.contact_capacity,island_storage+island_cursor*native.island_size,m.island_count,m.island_capacity,m.island_ids};island_cursor+=m.island_capacity;}
  else if(c)return fail(Error::InvalidArgument);
  if(!spAssembleOwnedSolverSet(set,m.set_index,&a))return fail(Error::InvalidArgument);
  // Re-export the assembled native set and require the exact owned membership.
  SpSolverMembership e{0,scratch.data(),scratch.data()+widest,scratch.data()+2*widest,scratch.data()+3*widest,0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest)};
  if(!spExportSolverMembership(set,&e)||e.set_index!=m.set_index)return fail(Error::InvalidArgument);
  if(m.set_index<0){if(e.body_count||e.state_count||e.joint_count||e.contact_count||e.island_count||e.body_capacity||e.state_capacity||e.joint_capacity||e.contact_capacity||e.island_capacity)return fail(Error::InvalidArgument);continue;}
  if(e.body_count!=m.body_count||e.body_capacity!=m.body_capacity||e.state_count!=m.state_count||e.state_capacity!=m.state_capacity||e.joint_count!=m.joint_count||e.joint_capacity!=m.joint_capacity||e.contact_count!=m.contact_count||e.contact_capacity!=m.contact_capacity||e.island_count!=m.island_count||e.island_capacity!=m.island_capacity||!same_ids(e.body_ids,m.body_ids,m.body_count)||!same_ids(e.joint_ids,m.joint_ids,m.joint_count)||!same_ids(e.contact_ids,m.contact_ids,m.contact_count)||!same_ids(e.island_ids,m.island_ids,m.island_count))return fail(Error::InvalidArgument);
 }
 if(island_cursor!=islands)return fail(Error::InvalidArgument);
 out.prior_.emplace(std::move(prior));return out;
}
}
