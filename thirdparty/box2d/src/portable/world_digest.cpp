// SPDX-License-Identifier: MIT
#include "world_digest.hpp"
#include "runtime_scratch_bridge.h"
#include "solver_payload_bridge.h"
#include <algorithm>
#include <memory>
#include <new>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Arena{std::byte*base{};size_t size{},used{};bool ok=true;
 template<class T>std::span<T>take(size_t n)noexcept{if(!ok)return {};const size_t a=alignof(T),pad=(a-(reinterpret_cast<uintptr_t>(base+used)%a))%a;if(used+pad>size){ok=false;return {};}const size_t avail=size-used-pad;if(n>avail/sizeof(T)){ok=false;return {};}used+=pad;auto*p=reinterpret_cast<T*>(base+used);used+=n*sizeof(T);for(size_t i=0;i<n;++i)std::construct_at(p+i);return {p,n};}};
struct Images{WorldRootImage root;ColdBodyImage cold_body;ColdContactImage cold_contact;ColdJointImage cold_joint;BodySimImage body;BodyStateImage state;ContactSimImage contact;JointSimImage joint;ShapeImage shape;TreeNodeImage node;BroadPhaseImage broadphase;ConstraintGraphImage graph;};
using Entry=std::pair<Identity,uint32_t>;
bool before(const Identity&a,const Identity&b)noexcept{if(a.kind!=b.kind)return a.kind<b.kind;if(a.simulation!=b.simulation)return a.simulation<b.simulation;return a.generation<b.generation;}
void hash_id(Sha256&h,Identity i)noexcept{h.update_u32(i.kind);h.update_u64(i.simulation);h.update_u64(i.generation);}
void hash_record(Sha256&h,const Record&r)noexcept{h.update_u32(0x31434552u);hash_id(h,r.identity);h.update_u32(uint32_t(r.fields.size()));for(const auto&f:r.fields){h.update_u32(f.id);h.update_u32(uint32_t(f.atoms.size()));for(const auto&a:f.atoms){h.update_u64(a.bits);hash_id(h,a.identity);}}}
// Live slots of a map ordered by canonical identity.
std::span<Entry> live(Arena&arena,const IdentityMap&map,size_t count)noexcept{auto out=arena.take<Entry>(count);size_t n=0;for(size_t i=0;i<count&&arena.ok;++i)if(auto id=map.canonical(uint32_t(i)))out[n++]={*id,uint32_t(i)};std::sort(out.begin(),out.begin()+std::ptrdiff_t(n),[](const Entry&a,const Entry&b){return before(a.first,b.first);});return out.first(n);}
Identity as(Identity i,uint32_t kind)noexcept{i.kind=kind;return i;}
uint32_t visit_failure=0;
}
uint32_t last_visit_failure()noexcept{return visit_failure;}
Status visit_world(const void*world,const WorldDigestMaps&m,Allocator&allocator,WorldRecordVisitor&visitor)noexcept{visit_failure=0;
 for(auto*p:m.objects)if(!p)return (visit_failure=1,fail(Error::InvalidArgument));for(auto*p:m.slots)if(!p)return (visit_failure=2,fail(Error::InvalidArgument));for(size_t t=0;t<3;++t)if(!m.nodes[t]||!m.proxies[t])return (visit_failure=3,fail(Error::InvalidArgument));
 if(!world||!m.sensors||!m.move_events||!m.body_history||!m.shape_history||!m.contact_history||!m.joint_history||!m.bindings)return (visit_failure=4,fail(Error::InvalidArgument));
 if(!spValidateRuntimeScratchBarrier(world))return (visit_failure=5,fail(Error::NotReady));
 SpWorldOwnershipView ov{};SpGeometryOwnershipView gv{};SpWorldEventArrays ev{};SpBroadPhaseView bp{};
 if(!spWorldRootOwnershipView(world,&ov)||!spWorldRootGeometryView(world,&ov,&gv)||!spExportWorldEvents(world,&ev)||!spExportBroadPhase(gv.broadphase,&bp))return (visit_failure=6,fail(Error::InvalidArgument));
 const size_t bc=ov.body_count,cc=ov.contact_count,jc=ov.joint_count,ic=ov.island_count,sc=ov.set_count,shc=gv.shape_count,chc=gv.chain_count,ns=gv.sensor_count;
 size_t nodes_total=0;for(size_t t=0;t<3;++t)nodes_total+=size_t(std::max(bp.trees[t].nodeCapacity,0));const size_t pairs=bp.pairSet.count;
 size_t events_total=ev.move.count+ev.sensor_begin.count+ev.contact_begin.count+ev.hit.count+ev.joint.count;for(int i=0;i<2;++i)events_total+=ev.sensor_end[i].count+ev.contact_end[i].count;
 const size_t big=bc+cc+jc+ic+sc+shc+chc+ns+nodes_total+2*pairs+events_total+16;if(big>400000)return (visit_failure=7,fail(Error::CapacityExceeded));
 const size_t per=16*sizeof(Atom)+8*sizeof(Identity)+2*sizeof(SpContactSim)+2*sizeof(SpJointSim)+2*sizeof(b2BodySim)+2*sizeof(b2BodyState)+64*sizeof(uint64_t)+2*sizeof(Entry);
 const size_t bytes=sizeof(Images)+alignof(std::max_align_t)+4096+big*per;
 void*block=allocator.allocate(bytes,alignof(std::max_align_t),MemoryDomain::Recovery);if(!block)return (visit_failure=8,fail(Error::OutOfMemory));
 struct Release{Allocator&a;void*p;Images*i;~Release(){if(i)std::destroy_at(i);a.deallocate(p);}};auto*images=new(block) Images();Release release{allocator,block,images};auto&im=*images;
 Arena arena{static_cast<std::byte*>(block)+sizeof(Images)+alignof(std::max_align_t),bytes-sizeof(Images)-alignof(std::max_align_t),0};
 const auto&body=*m.objects[SP_POOL_BODY];const auto&contact=*m.objects[SP_POOL_CONTACT];const auto&joint=*m.objects[SP_POOL_JOINT];const auto&island=*m.objects[SP_POOL_ISLAND];const auto&set=*m.objects[SP_POOL_SET];const auto&shape=*m.objects[SP_POOL_SHAPE];const auto&chain=*m.objects[SP_POOL_CHAIN];
 const TreeCaptureBoundary boundary{true,true,0};const std::array<const IdentityMap*,3> proxies{m.proxies[0],m.proxies[1],m.proxies[2]};
 auto record=[&](RecordTag tag,const Record&r,uint32_t index=0){visitor.record(tag,index,r);};
 // Root scalars and bindings.
 {SpWorldRoot r{};if(!spExportWorldRoot(world,&r))return (visit_failure=9,fail(Error::NotReady));if(auto s=capture_world_root(r,m.root,island,*m.bindings,im.root);!s)return (visit_failure=10,fail(s.error()));record(RecordTag::Root,im.root.record);}
 // Pools: free order and per-slot occupant and native generation by slot identity.
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){const size_t mark=arena.used;SpPoolView pv{};if(!spWorldPoolView(world,p,&pv))return (visit_failure=11,fail(Error::InvalidArgument));visitor.pool_header(p,pv.allocated_count,pv.free_count);
  for(uint32_t k=0;k<pv.free_count;++k){auto id=m.slots[p]->canonical(uint32_t(pv.free_entries[k]));if(!id)return (visit_failure=12,fail(id.error()));visitor.pool_free(p,*id);}
  auto slots=live(arena,*m.slots[p],pv.allocated_count);if(!arena.ok)return (visit_failure=13,fail(Error::CapacityExceeded));if(slots.size()!=pv.allocated_count)return (visit_failure=1400+p,fail(Error::InvalidArgument));
  for(const auto&[sid,slot]:slots){uint32_t g=0;if(!spWorldSlotGeneration(world,p,slot,&g))return (visit_failure=15,fail(Error::InvalidArgument));auto occupant=m.objects[p]->canonical(slot);visitor.pool_slot(p,sid,occupant?*occupant:Identity{},g);}
  arena.used=mark;}
 auto bindings=*m.bindings;
 // Cold bodies.
 {const size_t mark=arena.used;const auto*bodies=static_cast<const b2Body*>(ov.bodies);auto order=live(arena,body,bc);if(!arena.ok)return (visit_failure=16,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){const auto&b=bodies[k];NativeBinding member{uint32_t(b.localIndex),id},islandMember{uint32_t(std::max(b.islandIndex,0)),id};uint32_t a=0,c=0;auto mm=IdentityMap::prepare(std::span(&member,1),std::span(&a,1),body_kind);auto imm=IdentityMap::prepare(b.islandIndex<0?std::span<NativeBinding>{}:std::span(&islandMember,1),std::span(&c,1),body_kind);if(!mm||!imm)return (visit_failure=17,fail(Error::InvalidArgument));
   ColdBodyMappings maps{body,set,*mm,shape,chain,island,*imm,*m.move_events,contact,joint,bindings};if(auto s=capture_cold_body(b,id,maps,im.cold_body);!s)return (visit_failure=18,fail(s.error()));record(RecordTag::ColdBody,im.cold_body.record);}
  arena.used=mark;}
 // Cold contacts and joints.
 const auto cold=spColdArrayLayout();auto cold_contacts=arena.take<SpColdContact>(cc);if(!arena.ok)return (visit_failure=19,fail(Error::CapacityExceeded));
 for(size_t k=0;k<cc;++k)if(!spExportColdContact(static_cast<const std::byte*>(ov.contacts)+k*cold.contact_size,&cold_contacts[k]))return (visit_failure=20,fail(Error::InvalidArgument));
 {const size_t mark=arena.used;auto order=live(arena,contact,cc);if(!arena.ok)return (visit_failure=21,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){const auto&c=cold_contacts[k];NativeBinding member{uint32_t(c.localIndex),id},islandMember{uint32_t(std::max(c.islandIndex,0)),id};uint32_t a=0,d=0;auto mm=IdentityMap::prepare(std::span(&member,1),std::span(&a,1),contact_kind);auto imm=IdentityMap::prepare(c.islandIndex<0?std::span<NativeBinding>{}:std::span(&islandMember,1),std::span(&d,1),contact_kind);if(!mm||!imm)return (visit_failure=22,fail(Error::InvalidArgument));
   ColdContactMappings maps{contact,shape,body,island,*imm,set,*mm};if(auto s=capture_cold_contact(c,id,maps,im.cold_contact);!s)return (visit_failure=23,fail(s.error()));record(RecordTag::ColdContact,im.cold_contact.record);}
  arena.used=mark;}
 {const size_t mark=arena.used;auto order=live(arena,joint,jc);if(!arena.ok)return (visit_failure=24,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){SpColdJoint j{};if(!spExportColdJoint(static_cast<const std::byte*>(ov.joints)+size_t(k)*cold.joint_size,&j))return (visit_failure=25,fail(Error::InvalidArgument));NativeBinding member{uint32_t(j.localIndex),id},islandMember{uint32_t(std::max(j.islandIndex,0)),id};uint32_t a=0,d=0;auto mm=IdentityMap::prepare(std::span(&member,1),std::span(&a,1),joint_kind);auto imm=IdentityMap::prepare(j.islandIndex<0?std::span<NativeBinding>{}:std::span(&islandMember,1),std::span(&d,1),joint_kind);if(!mm||!imm)return (visit_failure=26,fail(Error::InvalidArgument));
   ColdJointMappings maps{joint,body,set,*mm,island,*imm,bindings};if(auto s=capture_cold_joint(j,id,maps,im.cold_joint);!s)return (visit_failure=27,fail(s.error()));record(RecordTag::ColdJoint,im.cold_joint.record);}
  arena.used=mark;}
 // Solver sets: membership, body simulations and states, non-graph constraints (normalized labels).
 const auto set_layout=spNativeSolverSetLayout();const size_t widest=std::max({bc,cc,jc,ic,size_t(1)});
 {const size_t mark=arena.used;auto order=live(arena,set,sc);if(!arena.ok)return (visit_failure=28,fail(Error::CapacityExceeded));
  for(const auto&[sid,slot]:order){const size_t inner=arena.used;const void*native=static_cast<const std::byte*>(ov.sets)+size_t(slot)*set_layout.size;
   auto ids=arena.take<int>(4*widest);std::array<std::span<Atom>,5> columns;for(auto&c:columns)c=arena.take<Atom>(widest);auto scratch=arena.take<Identity>(widest);if(!arena.ok)return (visit_failure=29,fail(Error::CapacityExceeded));
   SpSolverMembership mm{0,ids.data(),ids.data()+widest,ids.data()+2*widest,ids.data()+3*widest,0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest)};if(!spExportSolverMembership(native,&mm))return (visit_failure=30,fail(Error::InvalidArgument));
   {SolverMembershipImage image(columns);SolverMembershipMaps maps{set,body,joint,contact,island};if(auto s=capture_solver_membership(mm,sid,maps,scratch,image);!s)return (visit_failure=31,fail(s.error()));record(RecordTag::SetMembership,image.record);}
   auto sims=arena.take<b2BodySim>(widest);auto states=arena.take<b2BodyState>(widest);auto joints=arena.take<SpJointSim>(widest);auto contacts=arena.take<SpContactSim>(widest);auto islands=arena.take<int>(widest);if(!arena.ok)return (visit_failure=32,fail(Error::CapacityExceeded));
   SpSolverPayload pl{mm.set_index,sims.data(),states.data(),joints.data(),contacts.data(),islands.data()};const uint32_t room[5]={uint32_t(widest),uint32_t(widest),uint32_t(widest),uint32_t(widest),uint32_t(widest)};if(!spExportSolverPayload(native,&pl,room))return (visit_failure=33,fail(Error::InvalidArgument));
   for(uint32_t i=0;i<pl.count[0];++i){auto id=body.canonical(uint32_t(sims[i].bodyId));if(!id)return (visit_failure=34,fail(id.error()));if(auto s=capture_body_sim(sims[i],as(*id,bodysim_kind),body,im.body);!s)return (visit_failure=35,fail(s.error()));record(RecordTag::BodySim,im.body.record);
    if(i<pl.count[1]){if(auto s=capture_body_state(states[i],as(*id,bodystate_kind),im.state);!s)return (visit_failure=36,fail(s.error()));record(RecordTag::BodyState,im.state.record);}}
   for(uint32_t i=0;i<pl.count[3];++i){SpContactSim c=contacts[i];c.bodySimIndexA=c.bodySimIndexB=-1;auto id=contact.canonical(uint32_t(c.contactId));if(!id)return (visit_failure=37,fail(id.error()));ContactMappings maps{contact,shape,body,body};if(auto s=capture_contact_sim(c,as(*id,contact_sim_kind),maps,im.contact);!s)return (visit_failure=38,fail(s.error()));record(RecordTag::SetContact,im.contact.record);}
   for(uint32_t i=0;i<pl.count[2];++i){SpJointSim j=joints[i];for(int e=0;e<2;++e)if(int*x=spInactiveJointIndex(&j,e))*x=-1;auto id=joint.canonical(uint32_t(j.jointId));if(!id)return (visit_failure=39,fail(id.error()));JointSimMappings maps{joint,body,body};if(auto s=capture_joint_sim(j,as(*id,joint_sim_kind),maps,im.joint);!s)return (visit_failure=40,fail(s.error()));for(size_t r=0;r<im.joint.count;++r)record(r?RecordTag::SetJointPayload:RecordTag::SetJoint,im.joint.records[r]);}
   arena.used=inner;}
  arena.used=mark;}
 // Islands.
 {const size_t mark=arena.used;const auto il=spNativeIslandLayout();auto order=live(arena,island,ic);auto members=arena.take<int>(widest);auto clinks=arena.take<SpIslandLink>(widest),jlinks=arena.take<SpIslandLink>(widest);auto ba=arena.take<Atom>(widest),ca=arena.take<Atom>(3*widest),ja=arena.take<Atom>(3*widest);auto scratch=arena.take<Identity>(widest);if(!arena.ok)return (visit_failure=41,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){SpIslandView v{0,0,0,0,members.data(),clinks.data(),jlinks.data(),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest)};if(!spExportIsland(static_cast<const std::byte*>(ov.islands)+size_t(k)*il.size,&v))return (visit_failure=42,fail(Error::InvalidArgument));
   NativeBinding member{uint32_t(v.local_index),id};uint32_t a=0;auto mm=IdentityMap::prepare(std::span(&member,1),std::span(&a,1),island_kind);if(!mm)return (visit_failure=43,fail(mm.error()));IslandMappings maps{island,set,*mm,body,contact,joint};IslandImage image(ba,ca,ja);if(auto s=capture_island(v,id,maps,scratch,image);!s)return (visit_failure=44,fail(s.error()));record(RecordTag::Island,image.record);}
  arena.used=mark;}
 // Constraint graph: colors, their constraints and the root.
 {const size_t mark=arena.used;const auto*bodies=static_cast<const b2Body*>(ov.bodies);
  auto roles=arena.take<GraphBodyRole>(bc);size_t nr=0;for(size_t k=0;k<bc;++k)if(body.canonical(uint32_t(k)))roles[nr++]={uint32_t(k),uint32_t(bodies[k].type)};
  auto ends=arena.take<GraphContactEndpoints>(cc);size_t ne=0;for(size_t k=0;k<cc&&arena.ok;++k)if(contact.canonical(uint32_t(k)))ends[ne++]={uint32_t(k),cold_contacts[k].edges[0].bodyId,cold_contacts[k].edges[1].bodyId};
  auto awake_ids=arena.take<int>(4*widest);auto awake_bind=arena.take<NativeBinding>(widest);auto awake_order=arena.take<uint32_t>(widest);if(!arena.ok)return (visit_failure=45,fail(Error::CapacityExceeded));
  SpSolverMembership am{0,awake_ids.data(),awake_ids.data()+widest,awake_ids.data()+2*widest,awake_ids.data()+3*widest,0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest),0,uint32_t(widest)};
  if(sc<3||!spExportSolverMembership(static_cast<const std::byte*>(ov.sets)+2*set_layout.size,&am))return (visit_failure=46,fail(Error::InvalidArgument));
  for(uint32_t i=0;i<am.body_count;++i){auto id=body.canonical(uint32_t(am.body_ids[i]));if(!id)return (visit_failure=47,fail(id.error()));awake_bind[i]={i,*id};}
  auto awake=IdentityMap::prepare(awake_bind.first(am.body_count),awake_order,body_kind);if(!awake)return (visit_failure=48,fail(awake.error()));
  const GraphColorMaps maps{body,contact,joint,roles.first(nr),*awake,ends.first(ne)};
  auto contact_buffer=arena.take<SpContactSim>(widest);auto joint_buffer=arena.take<SpJointSim>(widest);auto views=arena.take<SpGraphColorView>(24);if(!arena.ok)return (visit_failure=49,fail(Error::CapacityExceeded));
  size_t cu=0,ju=0;for(uint32_t c=0;c<24;++c){const uint32_t n=spOwnedGraphColorCount(ov.graph,c,false),q=spOwnedGraphColorCount(ov.graph,c,true);if(cu+n>widest||ju+q>widest)return (visit_failure=50,fail(Error::CapacityExceeded));views[c].contacts=contact_buffer.data()+cu;views[c].joints=joint_buffer.data()+ju;cu+=n;ju+=q;if(!spExportGraphColor(spOwnedGraphColor(ov.graph,c),&views[c],n,q))return (visit_failure=51,fail(Error::InvalidArgument));for(uint32_t k=0;k<views[c].contact_count;++k)views[c].contacts[k].bodySimIndexA=views[c].contacts[k].bodySimIndexB=-1;for(uint32_t k=0;k<views[c].joint_count;++k)for(int e=0;e<2;++e)if(int*x=spInactiveJointIndex(&views[c].joints[k],e))*x=-1;}
  auto color_scratch=arena.take<uint32_t>(3*(cc+jc)+bc+8);auto ga=arena.take<Atom>(widest),gc=arena.take<Atom>(widest),gj=arena.take<Atom>(widest);if(!arena.ok)return (visit_failure=52,fail(Error::CapacityExceeded));
  for(uint32_t c=0;c<24;++c){GraphColorImage image({ga,gc,gj});if(auto s=capture_graph_color(views[c],c,m.colors[c],maps,boundary,color_scratch,image);!s)return (visit_failure=53,fail(s.error()));record(RecordTag::GraphColor,image.record,c);
   for(uint32_t k=0;k<views[c].contact_count;++k){auto id=contact.canonical(uint32_t(views[c].contacts[k].contactId));if(!id)return (visit_failure=54,fail(id.error()));ContactMappings cm{contact,shape,body,*awake};if(auto s=capture_contact_sim(views[c].contacts[k],as(*id,contact_sim_kind),cm,im.contact);!s)return (visit_failure=55,fail(s.error()));record(RecordTag::GraphContact,im.contact.record,c);}
   for(uint32_t k=0;k<views[c].joint_count;++k){auto id=joint.canonical(uint32_t(views[c].joints[k].jointId));if(!id)return (visit_failure=56,fail(id.error()));JointSimMappings jm{joint,body,*awake};if(auto s=capture_joint_sim(views[c].joints[k],as(*id,joint_sim_kind),jm,im.joint);!s)return (visit_failure=57,fail(s.error()));for(size_t r=0;r<im.joint.count;++r)record(r?RecordTag::GraphJointPayload:RecordTag::GraphJoint,im.joint.records[r],c);}}
  auto identity_scratch=arena.take<uint32_t>(cc+jc+8);if(!arena.ok)return (visit_failure=58,fail(Error::CapacityExceeded));ConstraintGraphScratch gs{color_scratch,identity_scratch,{ga,gc,gj}};
  if(auto s=capture_constraint_graph(views.first(24),m.colors,m.graph,maps,boundary,gs,im.graph);!s)return (visit_failure=59,fail(s.error()));record(RecordTag::GraphRoot,im.graph.record);
  arena.used=mark;}
 // Shapes with tagged geometry, chains and dense sensors.
 {const size_t mark=arena.used;const auto*shapes=static_cast<const b2Shape*>(gv.shapes);auto order=live(arena,shape,shc);if(!arena.ok)return (visit_failure=60,fail(Error::CapacityExceeded));ShapeMappings maps{shape,body,*m.sensors,chain,bindings,proxies};
  for(const auto&[id,k]:order){if(auto s=capture_shape(shapes[k],id,maps,im.shape);!s)return (visit_failure=61,fail(s.error()));record(RecordTag::ShapeBase,im.shape.records[0]);record(RecordTag::ShapeGeometry,im.shape.records[1]);}
  arena.used=mark;}
 {const size_t mark=arena.used;const auto geo=spGeometryLayout();auto order=live(arena,chain,chc);if(!arena.ok)return (visit_failure=62,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){const size_t inner=arena.used;const auto*n=reinterpret_cast<const b2ChainShape*>(static_cast<const std::byte*>(gv.chains)+size_t(k)*geo.chain_size);const size_t count=size_t(std::max(n->count,0)),materials=size_t(std::max(n->materialCount,0));
   auto ids=arena.take<int>(count);auto mats=arena.take<b2SurfaceMaterial>(materials);std::array<std::span<Atom>,7> columns;columns[0]=arena.take<Atom>(count);for(size_t c=1;c<7;++c)columns[c]=arena.take<Atom>(materials);auto scratch=arena.take<Identity>(count);if(!arena.ok)return (visit_failure=63,fail(Error::CapacityExceeded));
   SpChainView v{};v.shapes=ids.data();v.materials=mats.data();v.shape_capacity=uint32_t(count);v.material_capacity=uint32_t(materials);if(!spExportChain(n,&v))return (visit_failure=64,fail(Error::InvalidArgument));ChainImage image(columns);if(auto s=capture_chain(v,id,ChainMaps{chain,body,shape},scratch,image);!s)return (visit_failure=65,fail(s.error()));record(RecordTag::Chain,image.record);arena.used=inner;}
  arena.used=mark;}
 {const size_t mark=arena.used;const auto geo=spGeometryLayout();auto order=live(arena,*m.sensors,ns);const size_t room=shc+8;auto hits=arena.take<SpVisitor>(room),first=arena.take<SpVisitor>(room),second=arena.take<SpVisitor>(room);std::array<std::span<Atom>,3> columns{arena.take<Atom>(room),arena.take<Atom>(room),arena.take<Atom>(room)};auto scratch=arena.take<Identity>(room);if(!arena.ok)return (visit_failure=66,fail(Error::CapacityExceeded));
  for(const auto&[id,k]:order){SpSensorView v{};v.hits=hits.data();v.overlaps1=first.data();v.overlaps2=second.data();v.hit_capacity=v.first_capacity=v.second_capacity=uint32_t(room);if(!spExportSensor(static_cast<const std::byte*>(gv.sensors)+size_t(k)*geo.sensor_size,&v))return (visit_failure=67,fail(Error::InvalidArgument));SensorImage image(columns);if(auto s=capture_sensor(v,id,shape,*m.shape_history,scratch,image);!s)return (visit_failure=68,fail(s.error()));record(RecordTag::Sensor,image.record);}
  arena.used=mark;}
 // Broadphase: tree nodes, trees, moves, pairs and the aggregate root.
 {const size_t mark=arena.used;auto marks=arena.take<uint8_t>(nodes_total+8);auto stack=arena.take<uint32_t>(std::max<size_t>(nodes_total,bp.moveArray.count)+8);auto audit=arena.take<uint64_t>(nodes_total+pairs+8);auto membership=arena.take<Atom>(nodes_total+8);if(!arena.ok)return (visit_failure=69,fail(Error::CapacityExceeded));
  for(size_t t=0;t<3;++t){const size_t inner=arena.used;const size_t cap=size_t(std::max(bp.trees[t].nodeCapacity,0));auto order=live(arena,*m.nodes[t],cap);if(!arena.ok)return (visit_failure=70,fail(Error::CapacityExceeded));if(order.size()!=cap)return (visit_failure=71,fail(Error::InvalidArgument));
   for(const auto&[id,k]:order){if(auto s=capture_tree_node(bp.trees[t].nodes[k],id,*m.nodes[t],shape,im.node);!s)return (visit_failure=72,fail(s.error()));record(RecordTag::TreeNode,im.node.record,uint32_t(t));}
   DynamicTreeImage image(membership.first(std::max<size_t>(cap,1)));if(auto s=capture_dynamic_tree(bp.trees[t],m.broadphase_ids.trees[t],*m.nodes[t],boundary,marks,stack,image);!s)return (visit_failure=73,fail(s.error()));record(RecordTag::Tree,image.record,uint32_t(t));arena.used=inner;}
  {const size_t n=size_t(std::max(bp.moveArray.count,0));auto refs=arena.take<Atom>(n+1),types=arena.take<Atom>(n+1);if(!arena.ok)return (visit_failure=74,fail(Error::CapacityExceeded));MovesImage image(refs,types);MoveBufferView mv{};for(size_t t=0;t<3;++t)mv.bits[t]=bp.movedProxies[t];mv.moves=bp.moveArray.data;mv.count=uint32_t(n);mv.capacity=uint32_t(std::max(bp.moveArray.capacity,0));if(auto s=capture_moves(mv,m.broadphase_ids.moves,MoveMappings{proxies},stack,image);!s)return (visit_failure=75,fail(s.error()));record(RecordTag::Moves,image.record);}
  {auto atoms=arena.take<Atom>(2*pairs+2);if(!arena.ok)return (visit_failure=76,fail(Error::CapacityExceeded));PairSetImage image(atoms);if(auto s=capture_pair_set(bp.pairSet,m.broadphase_ids.pairs,shape,image);!s)return (visit_failure=77,fail(s.error()));record(RecordTag::Pairs,image.record);}
  if(auto s=capture_broadphase(bp,m.broadphase,m.broadphase_ids,BroadPhaseMaps{m.nodes,proxies,&shape},boundary,BroadPhaseScratch{marks,stack,audit},im.broadphase);!s)return (visit_failure=78,fail(s.error()));record(RecordTag::Broadphase,im.broadphase.record);
  arena.used=mark;}
 // Events.
 {const EventMaps maps{*m.body_history,*m.shape_history,*m.contact_history,*m.joint_history,bindings,m.world0};
  const std::array<std::pair<EventKind,SpEventArray>,9> arrays{{{EventKind::BodyMove,ev.move},{EventKind::SensorBegin,ev.sensor_begin},{EventKind::ContactBegin,ev.contact_begin},{EventKind::SensorEnd,ev.sensor_end[0]},{EventKind::SensorEnd,ev.sensor_end[1]},{EventKind::ContactEnd,ev.contact_end[0]},{EventKind::ContactEnd,ev.contact_end[1]},{EventKind::ContactHit,ev.hit},{EventKind::Joint,ev.joint}}};
  for(size_t a=0;a<9;++a){const size_t mark=arena.used;auto storage=arena.take<Atom>(1+event_columns(arrays[a].first)*arrays[a].second.count);if(!arena.ok)return (visit_failure=79,fail(Error::CapacityExceeded));EventArrayImage image(storage);if(auto s=capture_event_array(arrays[a].first,arrays[a].second,m.events[a],maps,m.move_events,image);!s)return (visit_failure=80,fail(s.error()));record(RecordTag::Event,image.record,uint32_t(a));arena.used=mark;}}
 return {};
}
namespace {
class DigestVisitor final:public WorldRecordVisitor{public:Sha256 h;
 DigestVisitor()noexcept{const char tag[]="superpos.box2d.world-digest.v1";h.update(std::as_bytes(std::span(tag)));}
 void record(RecordTag,uint32_t,const Record&r)noexcept override{hash_record(h,r);}
 void pool_header(uint32_t p,uint32_t allocated,uint32_t free)noexcept override{h.update_u32(0x4c4f4f50u);h.update_u32(p);h.update_u32(allocated);h.update_u32(free);}
 void pool_free(uint32_t,Identity slot)noexcept override{hash_id(h,slot);}
 void pool_slot(uint32_t,Identity slot,Identity occupant,uint32_t generation)noexcept override{hash_id(h,slot);hash_id(h,occupant);h.update_u32(generation);}};
}
Result<WorldDigest> digest_world(const void*world,const WorldDigestMaps&m,Allocator&allocator)noexcept{DigestVisitor v;if(auto s=visit_world(world,m,allocator,v);!s)return fail(s.error());return v.h.finish();}
}
