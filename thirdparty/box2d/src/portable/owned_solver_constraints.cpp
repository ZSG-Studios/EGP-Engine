// SPDX-License-Identifier: MIT
#include "owned_solver_constraints.hpp"
#include <algorithm>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}template<class T>size_t add(size_t n)noexcept{return raw(n,sizeof(T),alignof(T));}};
template<class T>std::span<T>take(void*p,Layout&l,size_t n)noexcept{auto*q=reinterpret_cast<T*>(static_cast<std::byte*>(p)+l.add<T>(n));for(size_t i=0;i<n;++i)std::construct_at(q+i);return {q,n};}
bool less(Identity a,Identity b)noexcept{return a.kind<b.kind||(a.kind==b.kind&&a.simulation<b.simulation);}
bool sorted(std::span<const Record>rs,uint32_t kind=0)noexcept{if(rs.size()>100000)return false;for(size_t i=0;i<rs.size();++i)if((kind&&rs[i].identity.kind!=kind)||!rs[i].identity.kind||!rs[i].identity.simulation||!rs[i].identity.generation||(i&&!less(rs[i-1].identity,rs[i].identity)))return false;return true;}
const Record*find(std::span<const Record>rs,Identity id)noexcept{auto it=std::lower_bound(rs.begin(),rs.end(),id,[](const Record&r,Identity v){return less(r.identity,v);});return it!=rs.end()&&it->identity==id?&*it:nullptr;}
const Record*payload(const Record&r,std::span<const Record>all)noexcept{for(auto f:r.fields)for(auto a:f.atoms)if(a.identity.kind>=distance_joint_kind&&a.identity.kind<=wheel_joint_kind)return find(all,a.identity);return nullptr;}
void sort_records(std::span<Record>rs)noexcept{std::sort(rs.begin(),rs.end(),[](const Record&a,const Record&b){return less(a.identity,b.identity);});}
// Owned placement of one live constraint: solver set/color/local and island link.
struct Place{int set=-1,color=-1,local=-1,island=-1,island_index=-1,link_a=-1,link_b=-1,type=-1,sim_a=-1,sim_b=-1;};
template<class Cold>bool reciprocal(std::span<const Cold>cold,std::span<const ConstraintEndpoints>ends,size_t id)noexcept{
 for(int e=0;e<2;++e){const int own=int(2*id)+e,body=cold[id].edges[e].bodyId;for(int key:{cold[id].edges[e].prevKey,cold[id].edges[e].nextKey}){if(key==-1)continue;if(key<0||size_t(key>>1)>=cold.size()||ends[size_t(key>>1)].body_a<0)return false;const auto&edge=cold[size_t(key>>1)].edges[key&1];if(edge.bodyId!=body||(key==cold[id].edges[e].prevKey?edge.nextKey:edge.prevKey)!=own)return false;}}
 return true;
}
}
OwnedSolverConstraints::OwnedSolverConstraints(OwnedSolverConstraints&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),sets_(std::exchange(o.sets_,{})),contact_ends_(std::exchange(o.contact_ends_,{})),joint_ends_(std::exchange(o.joint_ends_,{})),cold_contacts_(std::exchange(o.cold_contacts_,{})),cold_joints_(std::exchange(o.cold_joints_,{})){if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedSolverConstraints::~OwnedSolverConstraints(){for(auto&s:sets_)std::destroy_at(&s);if(block_)allocator_->deallocate(block_);}
size_t OwnedSolverConstraints::reserved_payload_bytes()const noexcept{size_t n=bytes_+(prior_?prior_->reserved_payload_bytes():0);for(const auto&s:sets_)if(s)n+=s->reserved_payload_bytes();return n;}
Result<OwnedSolverConstraints>restore_owned_solver_constraints(OwnedConstraintGraph&&prior,OwnedConstraintRecords in,Allocator&allocator)noexcept{return restore_owned_solver_constraints(std::move(prior),in,allocator,nullptr);}
Result<OwnedSolverConstraints>restore_owned_solver_constraints(OwnedConstraintGraph&&prior,OwnedConstraintRecords in,Allocator&allocator,const PointerIdentityMap*admitted)noexcept{
 if(!prior.native_graph())return fail(Error::InvalidArgument);
 const auto&islands=prior.islands();const auto&owned=islands.solver_bodies();const auto&pools=owned.pools();
 const auto*cp=pools.pool(SP_POOL_CONTACT),*jp=pools.pool(SP_POOL_JOINT),*bp=pools.pool(SP_POOL_BODY);if(!cp||!jp||!bp)return fail(Error::InvalidArgument);
 const auto&body_map=bp->object_map();const auto&contact_map=cp->object_map();const auto&joint_map=jp->object_map();const auto&shape_map=pools.pool(SP_POOL_SHAPE)->object_map();const auto&set_map=pools.pool(SP_POOL_SET)->object_map();const auto&island_map=pools.pool(SP_POOL_ISLAND)->object_map();
 const size_t cc=cp->pool().allocated_count,jc=jp->pool().allocated_count,bc=bp->pool().allocated_count;if(cc>100000||jc>100000||cp->pool().free_count>cc||jp->pool().free_count>jc)return fail(Error::InvalidArgument);
 const size_t live_contacts=cc-cp->pool().free_count,live_joints=jc-jp->pool().free_count;
 if(!sorted(in.cold_contacts,contact_kind)||!sorted(in.cold_joints,joint_kind)||!sorted(in.contacts,contact_sim_kind)||!sorted(in.joints,joint_sim_kind)||!sorted(in.joint_payloads)||in.cold_contacts.size()!=live_contacts||in.cold_joints.size()!=live_joints)return fail(Error::InvalidArgument);
 const auto sets=owned.sets();size_t inactive_contacts=0,inactive_joints=0,max_contacts=0,max_joints=0;
 for(const auto&s:sets)if(s.membership.set_index>=0){inactive_contacts+=s.membership.contact_count;inactive_joints+=s.membership.joint_count;max_contacts=std::max(max_contacts,size_t(s.membership.contact_count));max_joints=std::max(max_joints,size_t(s.membership.joint_count));}
 if(in.contacts.size()!=inactive_contacts||in.joints.size()!=inactive_joints||in.joint_payloads.size()>inactive_joints)return fail(Error::InvalidArgument);
 Layout plan;plan.add<Place>(cc);plan.add<Place>(jc);plan.add<ConstraintEndpoints>(cc);plan.add<ConstraintEndpoints>(jc);plan.add<SpColdContact>(cc);plan.add<SpColdJoint>(jc);plan.add<std::optional<OwnedInactiveConstraints>>(sets.size());plan.add<Record>(max_contacts);plan.add<Record>(max_joints);plan.add<Record>(max_joints);plan.add<SpIslandLink>(cc);plan.add<SpIslandLink>(jc);if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedSolverConstraints out;out.allocator_=&allocator;out.bytes_=std::max(plan.bytes,size_t(1));out.block_=allocator.allocate(out.bytes_,alignof(std::max_align_t),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout cursor;auto contact_place=take<Place>(out.block_,cursor,cc),joint_place=take<Place>(out.block_,cursor,jc);out.contact_ends_=take<ConstraintEndpoints>(out.block_,cursor,cc);out.joint_ends_=take<ConstraintEndpoints>(out.block_,cursor,jc);auto cold_contacts=take<SpColdContact>(out.block_,cursor,cc);auto cold_joints=take<SpColdJoint>(out.block_,cursor,jc);out.cold_contacts_=cold_contacts;out.cold_joints_=cold_joints;out.sets_=take<std::optional<OwnedInactiveConstraints>>(out.block_,cursor,sets.size());
 auto sub_contacts=take<Record>(out.block_,cursor,max_contacts),sub_joints=take<Record>(out.block_,cursor,max_joints),sub_payloads=take<Record>(out.block_,cursor,max_joints);auto contact_links=take<SpIslandLink>(out.block_,cursor,cc),joint_links=take<SpIslandLink>(out.block_,cursor,jc);
 if(cursor.bytes!=plan.bytes)return fail(Error::InvalidArgument);
 // Owned non-graph placement (sets), active graph placement and island links.
 size_t placed_contacts=0,placed_joints=0;
 for(const auto&s:sets){const auto&m=s.membership;if(m.set_index<0)continue;
  for(uint32_t i=0;i<m.contact_count;++i){int id=m.contact_ids[i];if(id<0||size_t(id)>=cc||contact_place[size_t(id)].set!=-1)return fail(Error::InvalidArgument);auto&p=contact_place[size_t(id)];p.set=m.set_index;p.local=int(i);++placed_contacts;}
  for(uint32_t i=0;i<m.joint_count;++i){int id=m.joint_ids[i];if(id<0||size_t(id)>=jc||joint_place[size_t(id)].set!=-1)return fail(Error::InvalidArgument);auto&p=joint_place[size_t(id)];p.set=m.set_index;p.local=int(i);++placed_joints;}}
 for(uint32_t color=0;color<24;++color){
  for(uint32_t i=0,n=spOwnedGraphColorCount(prior.native_graph(),color,false);i<n;++i){int id=-1;if(!spOwnedGraphContactAt(prior.native_graph(),color,i,&id)||id<0||size_t(id)>=cc||contact_place[size_t(id)].set!=-1)return fail(Error::InvalidArgument);auto&p=contact_place[size_t(id)];p.set=2;p.color=int(color);p.local=int(i);++placed_contacts;}
  for(uint32_t i=0,n=spOwnedGraphColorCount(prior.native_graph(),color,true);i<n;++i){int id=-1,type=-1,a=-1,b=-1;if(!spOwnedGraphJointAt(prior.native_graph(),color,i,&id,&type,&a,&b)||id<0||size_t(id)>=jc||joint_place[size_t(id)].set!=-1)return fail(Error::InvalidArgument);auto&p=joint_place[size_t(id)];p.set=2;p.color=int(color);p.local=int(i);p.type=type;p.sim_a=a;p.sim_b=b;++placed_joints;}}
 if(placed_contacts!=live_contacts||placed_joints!=live_joints)return fail(Error::InvalidArgument);
 for(uint32_t i=0;i<islands.allocated_count();++i){auto descriptor=spOwnedIslandDescriptor(islands.native_island(i));SpIslandView view{0,0,0,0,descriptor.bodies,contact_links.data(),joint_links.data(),0,descriptor.body_capacity,0,uint32_t(cc),0,uint32_t(jc)};if(!spExportIsland(islands.native_island(i),&view))return fail(Error::InvalidArgument);
  for(uint32_t k=0;k<view.contact_count;++k){auto l=view.contacts[k];if(l.object<0||size_t(l.object)>=cc||contact_place[size_t(l.object)].island!=-1)return fail(Error::InvalidArgument);auto&p=contact_place[size_t(l.object)];p.island=int(i);p.island_index=int(k);p.link_a=l.body_a;p.link_b=l.body_b;}
  for(uint32_t k=0;k<view.joint_count;++k){auto l=view.joints[k];if(l.object<0||size_t(l.object)>=jc||joint_place[size_t(l.object)].island!=-1)return fail(Error::InvalidArgument);auto&p=joint_place[size_t(l.object)];p.island=int(i);p.island_index=int(k);p.link_a=l.body_a;p.link_b=l.body_b;}}
 auto body_ok=[&](int b)noexcept{return b>=0&&size_t(b)<bc;};
 // Cold contacts: per-record membership maps encode the owned positions, so the
 // record must name itself at exactly that set/color/local and island slot.
 for(const auto&r:in.cold_contacts){auto id=contact_map.native(r.identity);if(!id||*id>=cc||out.contact_ends_[*id].body_a!=-1)return fail(Error::StaleGeneration);const auto&p=contact_place[*id];
  NativeBinding member{uint32_t(p.local),r.identity},island_member{uint32_t(std::max(p.island_index,0)),r.identity};uint32_t mo=0,io=0;auto member_map=IdentityMap::prepare(std::span(&member,1),std::span(&mo,1),contact_kind);auto island_member_map=IdentityMap::prepare(p.island>=0?std::span(&island_member,1):std::span<NativeBinding>{},std::span(&io,1),contact_kind);if(!member_map||!island_member_map)return fail(Error::InvalidArgument);
  ColdContactMappings maps{contact_map,shape_map,body_map,island_map,*island_member_map,set_map,*member_map};auto&c=cold_contacts[*id];if(auto s=restore_cold_contact(r,maps,c);!s)return fail(s.error());
  if(c.contactId!=int(*id)||c.setIndex!=p.set||c.colorIndex!=p.color||c.localIndex!=p.local||c.islandId!=p.island||c.islandIndex!=p.island_index||c.generation!=cp->native_generations()[*id])return fail(Error::InvalidArgument);
  const int a=c.edges[0].bodyId,b=c.edges[1].bodyId;if(!body_ok(a)||!body_ok(b)||a==b||spColdContactTouching(&c)!=(p.island>=0)||(p.color>=0&&!spColdContactTouching(&c))||(p.island>=0&&(p.link_a!=a||p.link_b!=b)))return fail(Error::InvalidArgument);
  out.contact_ends_[*id]={a,b};}
 for(size_t id=0;id<cc;++id)if(out.contact_ends_[id].body_a!=-1&&!reciprocal<SpColdContact>(cold_contacts,out.contact_ends_,id))return fail(Error::InvalidArgument);
 auto empty_bindings=PointerIdentityMap::prepare({},{},binding_kind);if(!empty_bindings)return fail(empty_bindings.error());const PointerIdentityMap*bindings=admitted?admitted:&*empty_bindings;
 for(const auto&r:in.cold_joints){auto id=joint_map.native(r.identity);if(!id||*id>=jc||out.joint_ends_[*id].body_a!=-1)return fail(Error::StaleGeneration);const auto&p=joint_place[*id];
  NativeBinding member{uint32_t(p.local),r.identity},island_member{uint32_t(std::max(p.island_index,0)),r.identity};uint32_t mo=0,io=0;auto member_map=IdentityMap::prepare(std::span(&member,1),std::span(&mo,1),joint_kind);auto island_member_map=IdentityMap::prepare(p.island>=0?std::span(&island_member,1):std::span<NativeBinding>{},std::span(&io,1),joint_kind);if(!member_map||!island_member_map)return fail(Error::InvalidArgument);
  ColdJointMappings maps{joint_map,body_map,set_map,*member_map,island_map,*island_member_map,*bindings};auto&j=cold_joints[*id];if(auto s=restore_cold_joint(r,maps,j);!s)return fail(s.error());
  if(j.jointId!=int(*id)||j.setIndex!=p.set||j.colorIndex!=p.color||j.localIndex!=p.local||j.islandId!=p.island||j.islandIndex!=p.island_index||j.generation!=jp->native_generations()[*id])return fail(Error::InvalidArgument);
  const int a=j.edges[0].bodyId,b=j.edges[1].bodyId;if(!body_ok(a)||!body_ok(b)||a==b||(p.set>=2)!=(p.island>=0)||(p.island>=0&&(p.link_a!=a||p.link_b!=b))||(p.color>=0&&(p.type!=int(j.type)||p.sim_a!=a||p.sim_b!=b)))return fail(Error::InvalidArgument);
  out.joint_ends_[*id]={a,b};}
 for(size_t id=0;id<jc;++id)if(out.joint_ends_[id].body_a!=-1&&!reciprocal<SpColdJoint>(cold_joints,out.joint_ends_,id))return fail(Error::InvalidArgument);
 // Per-set non-graph arrays with cold-derived endpoints.
 size_t used_payloads=0;InactiveConstraintMaps maps{contact_map,joint_map,body_map,shape_map};
 for(size_t slot=0;slot<sets.size();++slot){const auto&m=sets[slot].membership;if(m.set_index<0)continue;size_t nc=0,nj=0,np=0;
  for(uint32_t i=0;i<m.contact_count;++i){auto identity=contact_map.canonical(uint32_t(m.contact_ids[i]));if(!identity)return fail(identity.error());Identity key=*identity;key.kind=contact_sim_kind;const Record*r=find(in.contacts,key);if(!r)return fail(Error::StaleGeneration);sub_contacts[nc++]=*r;}
  for(uint32_t i=0;i<m.joint_count;++i){auto identity=joint_map.canonical(uint32_t(m.joint_ids[i]));if(!identity)return fail(identity.error());Identity key=*identity;key.kind=joint_sim_kind;const Record*r=find(in.joints,key);if(!r)return fail(Error::StaleGeneration);sub_joints[nj++]=*r;if(const Record*child=payload(*r,in.joint_payloads))sub_payloads[np++]=*child;}
  sort_records(sub_contacts.first(nc));sort_records(sub_joints.first(nj));sort_records(sub_payloads.first(np));
  auto restored=restore_inactive_constraints(m,{sub_contacts.first(nc),sub_joints.first(nj),sub_payloads.first(np)},maps,out.contact_ends_,out.joint_ends_,allocator);if(!restored)return fail(restored.error());
  for(uint32_t i=0;i<restored->joint_count();++i){SpJointSim sim{};if(!spExportJointSim(restored->native_joint(i),&sim)||int(sim.type)!=int(cold_joints[size_t(m.joint_ids[i])].type))return fail(Error::InvalidArgument);}
  used_payloads+=np;out.sets_[slot].emplace(std::move(*restored));}
 if(used_payloads!=in.joint_payloads.size())return fail(Error::InvalidArgument);
 out.prior_.emplace(std::move(prior));return out;
}
}
