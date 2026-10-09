// SPDX-License-Identifier: MIT
#include "owned_solver_world.hpp"
#include <algorithm>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}template<class T>size_t add(size_t n)noexcept{return raw(n,sizeof(T),alignof(T));}};
void*raw_take(void*p,Layout&l,size_t n,size_t s,size_t a)noexcept{return static_cast<std::byte*>(p)+l.raw(n,s,a);}
template<class T>std::span<T>take(void*p,Layout&l,size_t n)noexcept{auto*q=reinterpret_cast<T*>(static_cast<std::byte*>(p)+l.add<T>(n));for(size_t i=0;i<n;++i)std::construct_at(q+i);return {q,n};}
bool sorted(std::span<const Record>rs,uint32_t kind)noexcept{if(rs.size()>100000)return false;for(size_t i=0;i<rs.size();++i)if(rs[i].identity.kind!=kind||!rs[i].identity.simulation||!rs[i].identity.generation||(i&&rs[i-1].identity.simulation>=rs[i].identity.simulation))return false;return true;}
struct BodyPlace{int set=-1,local=-1,island=-1,island_index=-1;bool cold=false;};
}
OwnedSolverWorld::OwnedSolverWorld(OwnedSolverWorld&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),bodies_(std::exchange(o.bodies_,nullptr)),contacts_(std::exchange(o.contacts_,nullptr)),joints_(std::exchange(o.joints_,nullptr)),contact_stride_(o.contact_stride_),joint_stride_(o.joint_stride_),view_(std::exchange(o.view_,{})){if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedSolverWorld::~OwnedSolverWorld(){if(block_)allocator_->deallocate(block_);}
Result<OwnedSolverWorld>restore_owned_solver_world(OwnedSolverSets&&prior,std::span<const Record>cold_bodies,const IdentityMap&move_events,Allocator&allocator)noexcept{return restore_owned_solver_world(std::move(prior),cold_bodies,move_events,allocator,nullptr);}
Result<OwnedSolverWorld>restore_owned_solver_world(OwnedSolverSets&&prior,std::span<const Record>cold_bodies,const IdentityMap&move_events,Allocator&allocator,const PointerIdentityMap*admitted)noexcept{
 if(!prior.has_storage())return fail(Error::InvalidArgument);
 const auto&constraints=prior.constraints();const auto&graph=constraints.graph();const auto&islands=graph.islands();const auto&owned=islands.solver_bodies();const auto&pools=owned.pools();
 const auto*bp=pools.pool(SP_POOL_BODY),*cp=pools.pool(SP_POOL_CONTACT),*jp=pools.pool(SP_POOL_JOINT);if(!bp||!cp||!jp)return fail(Error::InvalidArgument);
 const size_t bc=bp->pool().allocated_count,cc=cp->pool().allocated_count,jc=jp->pool().allocated_count;
 if(bc>100000||bp->pool().free_count>bc||!sorted(cold_bodies,body_kind)||cold_bodies.size()!=bc-bp->pool().free_count||constraints.cold_contacts().size()!=cc||constraints.cold_joints().size()!=jc||constraints.contact_endpoints().size()!=cc||constraints.joint_endpoints().size()!=jc)return fail(Error::InvalidArgument);
 const auto cold=spColdArrayLayout();if(!cold.contact_size||!cold.joint_size)return fail(Error::IncompatibleSchema);
 Layout plan;plan.add<b2Body>(bc);plan.raw(cc,cold.contact_size,cold.contact_alignment);plan.raw(jc,cold.joint_size,cold.joint_alignment);plan.add<BodyPlace>(bc);plan.add<SpIslandLink>(cc);plan.add<SpIslandLink>(jc);if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedSolverWorld out;out.allocator_=&allocator;out.bytes_=std::max(plan.bytes,size_t(1));out.block_=allocator.allocate(out.bytes_,std::max({alignof(std::max_align_t),alignof(b2Body),size_t(cold.contact_alignment),size_t(cold.joint_alignment)}),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout cursor;auto bodies=take<b2Body>(out.block_,cursor,bc);out.bodies_=bodies.data();out.contacts_=raw_take(out.block_,cursor,cc,cold.contact_size,cold.contact_alignment);out.joints_=raw_take(out.block_,cursor,jc,cold.joint_size,cold.joint_alignment);out.contact_stride_=cold.contact_size;out.joint_stride_=cold.joint_size;auto place=take<BodyPlace>(out.block_,cursor,bc);auto contact_links=take<SpIslandLink>(out.block_,cursor,cc),joint_links=take<SpIslandLink>(out.block_,cursor,jc);if(cursor.bytes!=plan.bytes)return fail(Error::InvalidArgument);
 // Owned placement: set/local from solver memberships, island slot from islands.
 for(const auto&s:owned.sets()){const auto&m=s.membership;if(m.set_index<0)continue;for(uint32_t i=0;i<m.body_count;++i){int id=m.body_ids[i];if(id<0||size_t(id)>=bc||place[size_t(id)].set!=-1)return fail(Error::InvalidArgument);place[size_t(id)].set=m.set_index;place[size_t(id)].local=int(i);}}
 for(uint32_t i=0;i<islands.allocated_count();++i){auto descriptor=spOwnedIslandDescriptor(islands.native_island(i));SpIslandView view{0,0,0,0,descriptor.bodies,contact_links.data(),joint_links.data(),0,descriptor.body_capacity,0,uint32_t(cc),0,uint32_t(jc)};if(!spExportIsland(islands.native_island(i),&view))return fail(Error::InvalidArgument);
  for(uint32_t k=0;k<view.body_count;++k){int id=view.bodies[k];if(id<0||size_t(id)>=bc||place[size_t(id)].island!=-1)return fail(Error::InvalidArgument);place[size_t(id)].island=int(i);place[size_t(id)].island_index=int(k);}}
 auto empty_bindings=PointerIdentityMap::prepare({},{},binding_kind);if(!empty_bindings)return fail(empty_bindings.error());const PointerIdentityMap*bindings=admitted?admitted:&*empty_bindings;
 const auto&body_map=bp->object_map();const auto&set_map=pools.pool(SP_POOL_SET)->object_map();const auto&shape_map=pools.pool(SP_POOL_SHAPE)->object_map();const auto&chain_map=pools.pool(SP_POOL_CHAIN)->object_map();const auto&island_map=pools.pool(SP_POOL_ISLAND)->object_map();
 for(const auto&r:cold_bodies){auto id=body_map.native(r.identity);if(!id||*id>=bc||place[*id].cold||place[*id].set<0)return fail(Error::StaleGeneration);auto&p=place[*id];p.cold=true;
  NativeBinding member{uint32_t(p.local),r.identity},island_member{uint32_t(std::max(p.island_index,0)),r.identity};uint32_t mo=0,io=0;auto member_map=IdentityMap::prepare(std::span(&member,1),std::span(&mo,1),body_kind);auto island_member_map=IdentityMap::prepare(p.island>=0?std::span(&island_member,1):std::span<NativeBinding>{},std::span(&io,1),body_kind);if(!member_map||!island_member_map)return fail(Error::InvalidArgument);
  ColdBodyMappings maps{body_map,set_map,*member_map,shape_map,chain_map,island_map,*island_member_map,move_events,cp->object_map(),jp->object_map(),*bindings};auto&b=bodies[*id];if(auto s=restore_cold_body(r,maps,b);!s)return fail(s.error());
  if(b.id!=int(*id)||b.setIndex!=p.set||b.localIndex!=p.local||b.islandId!=p.island||b.islandIndex!=p.island_index||b.generation!=bp->native_generations()[*id])return fail(Error::InvalidArgument);}
 for(size_t id=0;id<bc;++id)if(!place[id].cold){if(place[id].set!=-1||place[id].island!=-1||bp->native_generations()[id]>UINT16_MAX)return fail(Error::InvalidArgument);auto&b=bodies[id];b=b2Body{};b.id=b.setIndex=b.localIndex=b.islandId=b.islandIndex=b.headContactKey=b.headJointKey=b.headShapeId=b.headChainId=b.bodyMoveIndex=-1;b.generation=uint16_t(bp->native_generations()[id]);}
 for(size_t id=0;id<cc;++id){void*slot=static_cast<std::byte*>(out.contacts_)+id*cold.contact_size;bool ok=constraints.contact_endpoints()[id].body_a>=0?spImportColdContact(&constraints.cold_contacts()[id],slot):spPrepareFreeColdContact(slot,cp->native_generations()[id]);if(!ok)return fail(Error::InvalidArgument);}
 for(size_t id=0;id<jc;++id){void*slot=static_cast<std::byte*>(out.joints_)+id*cold.joint_size;bool ok=constraints.joint_endpoints()[id].body_a>=0?spImportColdJoint(&constraints.cold_joints()[id],slot):spPrepareFreeColdJoint(slot,jp->native_generations()[id]);if(!ok)return fail(Error::InvalidArgument);}
 out.view_={out.bodies_,out.contacts_,out.joints_,islands.allocated_count()?islands.native_island(0):nullptr,prior.native_set(0),graph.native_graph(),uint32_t(bc),uint32_t(cc),uint32_t(jc),islands.allocated_count(),prior.set_count()};
 // Native validator: reciprocal cold/solver/island membership, list heads, counts and edge lists.
 if(!spValidateWorldOwnership(&out.view_))return fail(Error::InvalidArgument);
 out.prior_.emplace(std::move(prior));return out;
}
}
