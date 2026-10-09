// SPDX-License-Identifier: MIT
#include "checkpoint_pipeline.hpp"
#include "owned_solver_constraints.hpp"
#include <algorithm>
#include <box2d/types.h>
namespace superpos::box2d_portable::fixture {using namespace canonical;
namespace {
constexpr uint32_t object_kinds[SP_POOL_COUNT]={body_kind,contact_kind,joint_kind,island_kind,solver_set_kind,shape_kind,chain_kind};
constexpr EventKind event_kinds[9]={EventKind::BodyMove,EventKind::SensorBegin,EventKind::ContactBegin,EventKind::SensorEnd,EventKind::SensorEnd,EventKind::ContactEnd,EventKind::ContactEnd,EventKind::ContactHit,EventKind::Joint};
bool before(const Identity&a,const Identity&b){if(a.kind!=b.kind)return a.kind<b.kind;if(a.simulation!=b.simulation)return a.simulation<b.simulation;return a.generation<b.generation;}
void sort_records(std::vector<Record>&v){std::sort(v.begin(),v.end(),[](const Record&a,const Record&b){return before(a.identity,b.identity);});}
const IdentityMap*add_map(MapStorage&s,std::vector<NativeBinding> b,uint32_t kind){std::sort(b.begin(),b.end(),[](const NativeBinding&x,const NativeBinding&y){return x.native_slot<y.native_slot;});const size_t n=b.size();s.bindings.push_back(std::move(b));s.orders.emplace_back(n+1);auto m=IdentityMap::prepare(s.bindings.back(),s.orders.back(),kind);if(!m)return nullptr;s.maps.push_back(std::make_unique<IdentityMap>(*m));return s.maps.back().get();}
const VersionedIdentityMap*add_versioned(MapStorage&s,std::vector<VersionedNativeBinding> b,uint32_t kind,uint32_t max_generation){std::sort(b.begin(),b.end(),[](const VersionedNativeBinding&x,const VersionedNativeBinding&y){return x.native.slot<y.native.slot||(x.native.slot==y.native.slot&&x.native.generation<y.native.generation);});const size_t n=b.size();s.versioned.push_back(std::move(b));s.orders.emplace_back(n+1);auto m=VersionedIdentityMap::prepare(s.versioned.back(),s.orders.back(),kind,max_generation);if(!m)return nullptr;s.versioned_maps.push_back(std::make_unique<VersionedIdentityMap>(*m));return s.versioned_maps.back().get();}
bool add_pointers(MapStorage&s,std::vector<void*> pointers){std::sort(pointers.begin(),pointers.end(),[](void*a,void*b){return reinterpret_cast<uintptr_t>(a)<reinterpret_cast<uintptr_t>(b);});pointers.erase(std::unique(pointers.begin(),pointers.end()),pointers.end());s.pointer_bindings.clear();for(size_t i=0;i<pointers.size();++i)s.pointer_bindings.push_back({pointers[i],{binding_kind,uint64_t(i+1),1}});s.pointer_order.assign(pointers.size()+1,0);auto m=PointerIdentityMap::prepare(s.pointer_bindings,s.pointer_order,binding_kind);if(!m)return false;s.pointers=std::make_unique<PointerIdentityMap>(*m);return true;}
bool add_symbolic_pointers(MapStorage&s,std::vector<void*> pointers,const SymbolRegistry&r){std::sort(pointers.begin(),pointers.end(),[](void*a,void*b){return reinterpret_cast<uintptr_t>(a)<reinterpret_cast<uintptr_t>(b);});pointers.erase(std::unique(pointers.begin(),pointers.end()),pointers.end());s.pointer_bindings.clear();
 for(void*p:pointers){auto symbol=r.symbol(p);if(!symbol)return false;s.pointer_bindings.push_back({p,{binding_kind,*symbol,1}});}
 s.pointer_order.assign(s.pointer_bindings.size()+1,0);auto m=PointerIdentityMap::prepare(s.pointer_bindings,s.pointer_order,binding_kind);if(!m)return false;s.pointers=std::make_unique<PointerIdentityMap>(*m);return true;}
uint32_t max_generation(uint32_t pool){return pool==SP_POOL_CONTACT?UINT32_MAX:UINT16_MAX;}
const IdentityMap*node_map(MapStorage&s,size_t t,size_t capacity){std::vector<NativeBinding> b;for(size_t i=0;i<capacity;++i)b.push_back({uint32_t(i),{tree_node_kind,(uint64_t(t+1)<<32)|uint64_t(i+1),1}});return add_map(s,std::move(b),tree_node_kind);}
// Shapes, contacts, bodies and joints named by events or sensor overlaps.
struct Reference{uint32_t pool,slot,generation;};
std::vector<Reference> named_lifetimes(const void*world,const SpGeometryOwnershipView&gv){
 std::vector<Reference> out;SpWorldEventArrays ev{};if(!spExportWorldEvents(world,&ev))return out;
 auto shape=[&](b2ShapeId id){if(id.index1>0)out.push_back({SP_POOL_SHAPE,uint32_t(id.index1-1),id.generation});};auto contact=[&](b2ContactId id){if(id.index1>0)out.push_back({SP_POOL_CONTACT,uint32_t(id.index1-1),id.generation});};
 for(uint32_t i=0;i<ev.move.count;++i){auto id=static_cast<const b2BodyMoveEvent*>(ev.move.data)[i].bodyId;out.push_back({SP_POOL_BODY,uint32_t(id.index1-1),id.generation});}
 for(uint32_t i=0;i<ev.sensor_begin.count;++i){const auto&e=static_cast<const b2SensorBeginTouchEvent*>(ev.sensor_begin.data)[i];shape(e.sensorShapeId);shape(e.visitorShapeId);}
 for(int b=0;b<2;++b)for(uint32_t i=0;i<ev.sensor_end[b].count;++i){const auto&e=static_cast<const b2SensorEndTouchEvent*>(ev.sensor_end[b].data)[i];shape(e.sensorShapeId);shape(e.visitorShapeId);}
 for(uint32_t i=0;i<ev.contact_begin.count;++i){const auto&e=static_cast<const b2ContactBeginTouchEvent*>(ev.contact_begin.data)[i];shape(e.shapeIdA);shape(e.shapeIdB);contact(e.contactId);}
 for(int b=0;b<2;++b)for(uint32_t i=0;i<ev.contact_end[b].count;++i){const auto&e=static_cast<const b2ContactEndTouchEvent*>(ev.contact_end[b].data)[i];shape(e.shapeIdA);shape(e.shapeIdB);contact(e.contactId);}
 for(uint32_t i=0;i<ev.hit.count;++i){const auto&e=static_cast<const b2ContactHitEvent*>(ev.hit.data)[i];shape(e.shapeIdA);shape(e.shapeIdB);contact(e.contactId);}
 for(uint32_t i=0;i<ev.joint.count;++i){auto id=static_cast<const b2JointEvent*>(ev.joint.data)[i].jointId;out.push_back({SP_POOL_JOINT,uint32_t(id.index1-1),id.generation});}
 const auto geo=spGeometryLayout();std::vector<SpVisitor> a(gv.shape_count+8),b(gv.shape_count+8),c(gv.shape_count+8);
 for(uint32_t i=0;i<gv.sensor_count;++i){SpSensorView v{};v.hits=a.data();v.overlaps1=b.data();v.overlaps2=c.data();v.hit_capacity=v.first_capacity=v.second_capacity=uint32_t(a.size());if(!spExportSensor(static_cast<const std::byte*>(gv.sensors)+size_t(i)*geo.sensor_size,&v))continue;for(uint32_t k=0;k<v.first_count;++k)out.push_back({SP_POOL_SHAPE,uint32_t(v.overlaps1[k].shape),v.overlaps1[k].generation});for(uint32_t k=0;k<v.second_count;++k)out.push_back({SP_POOL_SHAPE,uint32_t(v.overlaps2[k].shape),v.overlaps2[k].generation});}
 std::sort(out.begin(),out.end(),[](const Reference&x,const Reference&y){return x.pool<y.pool||(x.pool==y.pool&&(x.slot<y.slot||(x.slot==y.slot&&x.generation<y.generation)));});
 out.erase(std::unique(out.begin(),out.end(),[](const Reference&x,const Reference&y){return x.pool==y.pool&&x.slot==y.slot&&x.generation==y.generation;}),out.end());return out;
}
std::vector<void*> world_pointers(const void*world,const SpWorldOwnershipView&ov,const SpGeometryOwnershipView&gv){
 std::vector<void*> p;SpWorldRoot r{};if(spExportWorldRoot(world,&r))for(void*x:{r.frictionCallback,r.restitutionCallback,r.preSolveFcn,r.preSolveContext,r.customFilterFcn,r.customFilterContext,r.userData})if(x)p.push_back(x);
 const auto*bodies=static_cast<const b2Body*>(ov.bodies);for(uint32_t i=0;i<ov.body_count;++i)if(bodies[i].id!=-1&&bodies[i].userData)p.push_back(bodies[i].userData);
 const auto*shapes=static_cast<const b2Shape*>(gv.shapes);for(uint32_t i=0;i<gv.shape_count;++i)if(shapes[i].id!=-1&&shapes[i].userData)p.push_back(shapes[i].userData);
 const auto cold=spColdArrayLayout();for(uint32_t i=0;i<ov.joint_count;++i){SpColdJoint j{};if(spExportColdJoint(static_cast<const std::byte*>(ov.joints)+size_t(i)*cold.joint_size,&j)&&j.jointId!=-1&&j.userData)p.push_back(j.userData);}
 return p;
}
class Copier final:public WorldRecordVisitor{WorldCheckpoint&cp;public:explicit Copier(WorldCheckpoint&c):cp(c){}
 void record(RecordTag tag,uint32_t index,const Record&r)noexcept override{Record v=cp.store(r);++cp.record_count;cp.ordered.push_back({uint32_t(tag)*100+index,v});switch(tag){
  case RecordTag::Root:cp.root=v;break;case RecordTag::ColdBody:cp.cold_bodies.push_back(v);break;case RecordTag::ColdContact:cp.cold_contacts.push_back(v);break;case RecordTag::ColdJoint:cp.cold_joints.push_back(v);break;
  case RecordTag::SetMembership:cp.sets.push_back(v);break;case RecordTag::BodySim:cp.bodies.push_back(v);break;case RecordTag::BodyState:cp.states.push_back(v);break;
  case RecordTag::SetContact:cp.set_contacts.push_back(v);break;case RecordTag::SetJoint:cp.set_joints.push_back(v);break;case RecordTag::SetJointPayload:cp.set_payloads.push_back(v);break;
  case RecordTag::Island:cp.islands.push_back(v);break;case RecordTag::GraphColor:if(index<24)cp.colors[index]=v;break;case RecordTag::GraphContact:cp.graph_contacts.push_back(v);break;case RecordTag::GraphJoint:cp.graph_joints.push_back(v);break;case RecordTag::GraphJointPayload:cp.graph_payloads.push_back(v);break;case RecordTag::GraphRoot:cp.graph=v;break;
  case RecordTag::ShapeBase:cp.shapes.push_back(v);break;case RecordTag::ShapeGeometry:cp.shape_geometry.push_back(v);break;case RecordTag::Chain:cp.chains.push_back(v);break;case RecordTag::Sensor:cp.sensors.push_back(v);break;
  case RecordTag::TreeNode:if(index<3)cp.nodes[index].push_back(v);break;case RecordTag::Tree:if(index<3)cp.trees[index]=v;break;case RecordTag::Moves:cp.moves=v;break;case RecordTag::Pairs:cp.pairs=v;break;case RecordTag::Broadphase:cp.broadphase=v;break;case RecordTag::Event:if(index<9)cp.events[index]=v;break;}}};
}
std::optional<SymbolRegistry> SymbolRegistry::make(std::vector<SymbolBinding> entries){
 SymbolRegistry r;for(const auto&e:entries)if(!e.symbol||!e.pointer)return std::nullopt;
 r.by_symbol_=entries;std::sort(r.by_symbol_.begin(),r.by_symbol_.end(),[](const SymbolBinding&a,const SymbolBinding&b){return a.symbol<b.symbol;});
 r.by_pointer_=entries;std::sort(r.by_pointer_.begin(),r.by_pointer_.end(),[](const SymbolBinding&a,const SymbolBinding&b){return reinterpret_cast<uintptr_t>(a.pointer)<reinterpret_cast<uintptr_t>(b.pointer);});
 for(size_t i=1;i<entries.size();++i)if(r.by_symbol_[i-1].symbol==r.by_symbol_[i].symbol||r.by_pointer_[i-1].pointer==r.by_pointer_[i].pointer)return std::nullopt;
 return r;
}
std::optional<uint64_t> SymbolRegistry::symbol(const void*p)const{auto it=std::lower_bound(by_pointer_.begin(),by_pointer_.end(),reinterpret_cast<uintptr_t>(p),[](const SymbolBinding&e,uintptr_t a){return reinterpret_cast<uintptr_t>(e.pointer)<a;});if(it==by_pointer_.end()||it->pointer!=p)return std::nullopt;return it->symbol;}
void*SymbolRegistry::pointer(uint64_t s)const{auto it=std::lower_bound(by_symbol_.begin(),by_symbol_.end(),s,[](const SymbolBinding&e,uint64_t x){return e.symbol<x;});return it==by_symbol_.end()||it->symbol!=s?nullptr:it->pointer;}
Record WorldCheckpoint::store(const Record&r){auto o=std::make_unique<OwnedRecord>();o->identity=r.identity;size_t n=0;for(const auto&f:r.fields)n+=f.atoms.size();o->atoms.reserve(n);for(const auto&f:r.fields)for(const auto&a:f.atoms)o->atoms.push_back(a);size_t at=0;for(const auto&f:r.fields){o->fields.push_back({f.id,std::span<const Atom>(o->atoms).subspan(at,f.atoms.size())});at+=f.atoms.size();}Record v{o->identity,o->fields};owned.push_back(std::move(o));return v;}
void assign_singleton_ids(WorldDigestMaps&m)noexcept{m.root={world_root_kind,1,1};m.graph={constraint_graph_kind,1,1};for(uint32_t c=0;c<24;++c)m.colors[c]={graph_color_kind,c+1,1};m.broadphase={broadphase_kind,1,1};for(uint32_t t=0;t<3;++t)m.broadphase_ids.trees[t]={dynamic_tree_kind,t+1,1};m.broadphase_ids.moves={moves_kind,1,1};m.broadphase_ids.pairs={pair_set_kind,1,1};for(uint32_t a=0;a<9;++a)m.events[a]={uint32_t(event_kinds[a]),a+1,1};}
std::optional<Identity> PersistentRegistries::identity(uint32_t pool,uint32_t slot)const{if(pool>=SP_POOL_COUNT)return std::nullopt;const auto&p=pools[pool];if(slot>=p.object_slots.size()||p.object_slots[slot].phase!=LifetimePhase::Live)return std::nullopt;return p.object_slots[slot].identity;}
namespace {
uint64_t next_identity(std::span<const LifetimeSlot> s,uint64_t first){uint64_t n=first;for(const auto&x:s)if(x.identity.simulation>=n)n=x.identity.simulation+1;return n;}
// Grows registry storage while keeping every slot's lifetime state and the
// registry's identity sequence.
Status ensure_registry(std::vector<LifetimeSlot>&slots,std::optional<LifetimeRegistry>&registry,uint32_t kind,uint32_t bits,uint64_t first,size_t need){
 if(registry&&slots.size()>=need)return {};
 const size_t capacity=std::min<size_t>(std::max<size_t>({need*2,size_t(64),slots.size()}),100000);if(capacity<need)return fail(Error::CapacityExceeded);
 const std::vector<LifetimeSlot> old=slots;const uint64_t next=registry?next_identity(old,first):first;registry.reset();slots.assign(capacity,LifetimeSlot{});
 auto r=LifetimeRegistry::create(slots,kind,bits,next);if(!r)return fail(r.error());registry.emplace(std::move(*r));std::copy(old.begin(),old.end(),slots.begin());return {};}
}
Status capture_checkpoint(const void*world,WorldCheckpoint&cp,Allocator&allocator,const LayoutPlan*plan,const SymbolRegistry*symbols,PersistentRegistries*persistent){
 SpWorldCaptureView v{};if(!spCaptureWorldStructure(world,&v))return (cp.failure_line=1,fail(Error::NotReady));
 if(persistent&&plan)for(const auto&o:plan->order)if(!o.empty())return (cp.failure_line=5,fail(Error::Unsupported));
 std::array<std::vector<LifetimeSlot>,SP_POOL_COUNT> object_slots,allocation_slots;std::array<std::optional<LifetimeRegistry>,SP_POOL_COUNT> local_objects,local_allocations;std::array<LifetimeRegistry*,SP_POOL_COUNT> objects{},allocations{};std::array<RegistryLease,SP_POOL_COUNT> object_leases{},allocation_leases{};
 // Every lease taken is released on every exit, so persistent registries stay usable after a failed capture.
 struct LeaseGuard{std::array<LifetimeRegistry*,SP_POOL_COUNT>&o,&a;std::array<RegistryLease,SP_POOL_COUNT>&ol,&al;std::array<bool,SP_POOL_COUNT> held{};~LeaseGuard(){for(size_t p=0;p<held.size();++p)if(held[p]){(void)o[p]->release(ol[p]);(void)a[p]->release(al[p]);}}} leases{objects,allocations,object_leases,allocation_leases};
 PersistentRegistries::Stats stats{};
 std::array<std::vector<NativeLifetime>,SP_POOL_COUNT> on,an;std::array<std::vector<uint8_t>,SP_POOL_COUNT> om,am;std::array<std::vector<NativeBinding>,SP_POOL_COUNT> ob,ab;std::array<std::vector<uint32_t>,SP_POOL_COUNT> oo,ao;
 std::array<std::optional<WorldOwnerMap>,5> owners;std::array<std::optional<GeometryOwnerMap>,2> geometries;std::array<std::optional<WorldPoolSlots>,SP_POOL_COUNT> slots;std::optional<GeometryOwnership> geometry;
 SpBroadPhaseView bp{};if(!spExportBroadPhase(v.geometry.broadphase,&bp))return (cp.failure_line=2,fail(Error::InvalidArgument));
 std::vector<uint8_t> gm(v.geometry.shape_count+1);const size_t keys=std::max<size_t>(v.ownership.contact_count,bp.pairSet.count)+1;std::vector<uint64_t> ck(keys),pk(keys);
 std::array<PoolOccupantDefinition,SP_POOL_COUNT> defs{};
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){auto d=pool_occupant_definition(SpWorldPool(p));if(!d)return (cp.failure_line=3,fail(d.error()));defs[p]=*d;const uint32_t count=v.pools[p].allocated_count;// Empty pools (a world without joints or chains) are captured as empty.
  const uint32_t bits=defs[p].native_generation_max==UINT16_MAX?16:defs[p].native_generation_max==UINT32_MAX?32:0;
  if(persistent){auto&P=persistent->pools[p];if(auto s=ensure_registry(P.object_slots,P.objects,defs[p].object_kind,bits,uint64_t(1000)*(p+1),count);!s)return (cp.failure_line=5,fail(s.error()));if(auto s=ensure_registry(P.allocation_slots,P.allocations,defs[p].slot_kind,0,uint64_t(100000)*(p+1),count);!s)return (cp.failure_line=5,fail(s.error()));objects[p]=&*P.objects;allocations[p]=&*P.allocations;
   for(uint32_t i=0;i<count;++i){SpNativeLifetime n{};if(!spObserveWorldCaptureSlot(&v,SpWorldPool(p),i,&n))return (cp.failure_line=6,fail(Error::InvalidArgument));auto&s=P.object_slots[i];const bool live=n.native_id!=-1;
    if(s.phase==LifetimePhase::Reserved)return (cp.failure_line=6,fail(Error::InvalidArgument));
    if(s.phase==LifetimePhase::Live){if(live&&n.generation==s.native_generation){++stats.survivors;continue;}if(auto st=objects[p]->retire(i,s.identity,{-1,s.native_generation});!st)return (cp.failure_line=9,fail(st.error()));++stats.retirements;}
    if(bits){const uint32_t last_dead=live?n.generation-1:n.generation;if((live&&n.generation<=s.native_generation)||(!live&&n.generation<s.native_generation))return (cp.failure_line=6,fail(Error::StaleGeneration));
     for(uint32_t g=s.native_generation+1;g<=last_dead;++g){auto t=objects[p]->reserve(i);if(!t)return (cp.failure_line=7,fail(t.error()));if(auto st=objects[p]->commit(*t,{int(i),g});!st)return (cp.failure_line=8,fail(st.error()));if(auto st=objects[p]->retire(i,t->identity,{-1,g});!st)return (cp.failure_line=9,fail(st.error()));}}
    if(live){auto t=objects[p]->reserve(i);if(!t)return (cp.failure_line=7,fail(t.error()));if(auto st=objects[p]->commit(*t,{n.native_id,n.generation});!st)return (cp.failure_line=8,fail(st.error()));++stats.births;}}
   for(uint32_t k=P.reserved;k<count;++k){auto t=allocations[p]->reserve(k);if(!t)return (cp.failure_line=9,fail(t.error()));if(auto st=allocations[p]->commit(*t,{int(k),0});!st)return (cp.failure_line=10,fail(st.error()));}P.reserved=std::max(P.reserved,count);
   cp.slot_base[p]=uint64_t(100000)*(p+1);cp.slot_source[p].clear();for(uint32_t k=0;k<count;++k)cp.slot_source[p].push_back(k);}
  else{object_slots[p].resize(std::max<uint32_t>(count,1));allocation_slots[p].resize(std::max<uint32_t>(count,1));
  auto r=LifetimeRegistry::create(object_slots[p],defs[p].object_kind,bits,uint64_t(1000)*(p+1)),a=LifetimeRegistry::create(allocation_slots[p],defs[p].slot_kind,0,uint64_t(100000)*(p+1));if(!r||!a)return (cp.failure_line=5,fail(Error::InvalidArgument));local_objects[p].emplace(std::move(*r));local_allocations[p].emplace(std::move(*a));objects[p]=&*local_objects[p];allocations[p]=&*local_allocations[p];
  for(uint32_t i=0;i<count;++i){SpNativeLifetime n{};if(!spObserveWorldCaptureSlot(&v,SpWorldPool(p),i,&n))return (cp.failure_line=6,fail(Error::InvalidArgument));// Replay earlier native generations of reused slots as retired lifetimes, so the registry observes the actual native generation sequence.
   const uint32_t past=bits?(n.native_id!=-1?n.generation-1:n.generation):0;for(uint32_t g=1;g<=past;++g){auto t=objects[p]->reserve(i);if(!t)return (cp.failure_line=7,fail(t.error()));if(auto s=objects[p]->commit(*t,{int(i),g});!s)return (cp.failure_line=8,fail(s.error()));if(auto s=objects[p]->retire(i,t->identity,{-1,g});!s)return (cp.failure_line=9,fail(s.error()));}
   if(n.native_id!=-1){auto t=objects[p]->reserve(i);if(!t)return (cp.failure_line=7,fail(t.error()));if(auto s=objects[p]->commit(*t,{n.native_id,n.generation});!s)return (cp.failure_line=8,fail(s.error()));}}
  const bool permuted=plan&&!plan->order[p].empty();if(permuted){if(plan->order[p].size()!=count)return (cp.failure_line=9,fail(Error::InvalidArgument));std::vector<uint8_t> seen(count);for(uint32_t s:plan->order[p]){if(s>=count||seen[s])return (cp.failure_line=9,fail(Error::InvalidArgument));seen[s]=1;}}
  cp.slot_base[p]=uint64_t(100000)*(p+1);cp.slot_source[p].clear();
  for(uint32_t k=0;k<count;++k){const uint32_t i=permuted?plan->order[p][k]:k;cp.slot_source[p].push_back(i);auto t=allocations[p]->reserve(i);if(!t)return (cp.failure_line=9,fail(t.error()));if(auto s=allocations[p]->commit(*t,{int(i),0});!s)return (cp.failure_line=10,fail(s.error()));}}
  {auto ol=objects[p]->lease();if(!ol)return (cp.failure_line=11,fail(Error::NotReady));auto al=allocations[p]->lease();if(!al){(void)objects[p]->release(*ol);return (cp.failure_line=11,fail(Error::NotReady));}object_leases[p]=*ol;allocation_leases[p]=*al;leases.held[p]=true;}
  auto ol=std::optional<RegistryLease>(object_leases[p]),al=std::optional<RegistryLease>(allocation_leases[p]);
  on[p].resize(count);om[p].resize(count);ob[p].resize(count);oo[p].resize(count);an[p].resize(count);am[p].resize(count);ab[p].resize(count);ao[p].resize(count);
  WorldMapStorage os{on[p],om[p],ob[p],oo[p]};auto free=std::span(v.pools[p].free_entries,v.pools[p].free_count);
  if(p<=SP_POOL_SET){auto m=derive_world_owner_map(v.ownership,SpOwnerKind(p),*objects[p],*ol,free,os);if(!m)return (cp.failure_line=12,fail(m.error()));owners[p]=*m;}
  else{if(!geometry){auto g=validate_geometry_ownership(v.geometry,*owners[0],{gm,ck,pk});if(!g)return (cp.failure_line=13,fail(g.error()));geometry=*g;}auto m=derive_geometry_map(*geometry,p==SP_POOL_CHAIN,*objects[p],*ol,free,os);if(!m)return (cp.failure_line=14,fail(m.error()));geometries[p-SP_POOL_SHAPE]=*m;}
  auto sm=derive_world_pool_slots(v,SpWorldPool(p),*allocations[p],*al,{an[p],am[p],ab[p],ao[p]});if(!sm)return (cp.failure_line=15,fail(sm.error()));slots[p]=*sm;}
 auto object_map=[&](uint32_t p)->const IdentityMap&{return p<=SP_POOL_SET?owners[p]->map():geometries[p-SP_POOL_SHAPE]->map();};
 // Pool occupant and free-order records.
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){const uint32_t count=v.pools[p].allocated_count;std::vector<PoolOccupantCell> cells(count);std::vector<Record> records(count);
  auto captured=p<=SP_POOL_SET?capture_pool_occupants(*slots[p],*owners[p],{cells,records}):capture_pool_occupants(*slots[p],*geometries[p-SP_POOL_SHAPE],{cells,records});if(!captured)return (cp.failure_line=16,fail(captured.error()));for(const auto&r:*captured)cp.pool_occupants[p].push_back(cp.store(r));
  // Occupant records are canonical in slot-identity order; a permuted plan
  // assigns slot identities out of native order.
  sort_records(cp.pool_occupants[p]);
  auto pd=pool_definition(defs[p].slot_kind,100000);if(!pd)return (cp.failure_line=17,fail(pd.error()));std::vector<Atom> free_atoms(count);std::vector<std::byte> visited(count);PoolImage image(free_atoms);if(auto s=capture_world_pool(*slots[p],{pd->record_kind,p+1,1},visited,image);!s)return (cp.failure_line=18,fail(s.error()));
  if(plan&&(plan->reverse_free>>p&1u)){if(image.record.fields.size()!=3)return (cp.failure_line=18,fail(Error::InvalidArgument));auto f=image.record.fields[2].atoms;std::reverse(const_cast<Atom*>(f.data()),const_cast<Atom*>(f.data())+f.size());}
  cp.pool_records[p]=cp.store(image.record);}
 // Source digest maps: lifetimes, leaf proxies, node slots, sensors, move events, histories, bindings.
 MapStorage ms;ms.bindings.reserve(64);ms.orders.reserve(64);ms.versioned.reserve(16);WorldDigestMaps&m=ms.digest;assign_singleton_ids(m);
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){m.objects[p]=&object_map(p);m.slots[p]=&slots[p]->map();}
 std::array<std::vector<NativeBinding>,3> pb;std::array<std::vector<uint32_t>,3> po;std::array<std::optional<IdentityMap>,3> proxies;
 for(uint32_t t=0;t<3;++t){pb[t].resize(v.geometry.shape_count+1);po[t].resize(v.geometry.shape_count+1);auto pm=derive_shape_proxy_map(*geometries[0],t,pb[t],po[t]);if(!pm)return (cp.failure_line=19,fail(pm.error()));proxies[t]=*pm;m.proxies[t]=&*proxies[t];m.nodes[t]=node_map(ms,t,size_t(std::max(bp.trees[t].nodeCapacity,0)));if(!m.nodes[t])return (cp.failure_line=20,fail(Error::InvalidArgument));}
 {const auto geo=spGeometryLayout();std::vector<SpVisitor> a(v.geometry.shape_count+8),b(a.size()),c(a.size());std::vector<NativeBinding> sb;
  for(uint32_t i=0;i<v.geometry.sensor_count;++i){SpSensorView s{};s.hits=a.data();s.overlaps1=b.data();s.overlaps2=c.data();s.hit_capacity=s.first_capacity=s.second_capacity=uint32_t(a.size());if(!spExportSensor(static_cast<const std::byte*>(v.geometry.sensors)+size_t(i)*geo.sensor_size,&s))return (cp.failure_line=21,fail(Error::InvalidArgument));auto shape=object_map(SP_POOL_SHAPE).canonical(uint32_t(s.shape));if(!shape)return (cp.failure_line=22,fail(shape.error()));sb.push_back({i,{sensor_kind,shape->simulation,shape->generation}});}
  m.sensors=add_map(ms,std::move(sb),sensor_kind);}
 SpWorldEventArrays ev{};if(!spExportWorldEvents(world,&ev))return (cp.failure_line=23,fail(Error::InvalidArgument));
 {std::vector<NativeBinding> mb;for(uint32_t i=0;i<ev.move.count;++i)mb.push_back({i,{body_move_kind,uint64_t(i)+1,1}});m.move_events=add_map(ms,std::move(mb),body_move_kind);}
 const auto named=named_lifetimes(world,v.geometry);
 std::array<const VersionedIdentityMap**,4> history_out{&m.body_history,&m.contact_history,&m.joint_history,&m.shape_history};const uint32_t history_pools[4]={SP_POOL_BODY,SP_POOL_CONTACT,SP_POOL_JOINT,SP_POOL_SHAPE};
 for(size_t h=0;h<4;++h){const uint32_t p=history_pools[h];std::vector<VersionedNativeBinding> vb;for(uint32_t i=0;i<v.pools[p].allocated_count;++i){SpNativeLifetime n{};spObserveWorldCaptureSlot(&v,SpWorldPool(p),i,&n);if(n.native_id==-1)continue;auto id=object_map(p).canonical(i);if(!id)return (cp.failure_line=24,fail(id.error()));vb.push_back({{i,n.generation},*id});}
  for(const auto&r:named){if(r.pool!=p)continue;SpNativeLifetime n{};const bool live=r.slot<v.pools[p].allocated_count&&spObserveWorldCaptureSlot(&v,SpWorldPool(p),r.slot,&n)&&n.native_id!=-1&&n.generation==r.generation;if(live)continue;auto slot=slots[p]->map().canonical(r.slot);if(!slot)return (cp.failure_line=25,fail(slot.error()));
   const Identity retired{object_kinds[p],(uint64_t(1)<<62)|(uint64_t(r.slot)<<32)|uint64_t(r.generation),1};vb.push_back({{r.slot,r.generation},retired});cp.retired.push_back({p,retired,*slot,r.generation});}
  *history_out[h]=add_versioned(ms,std::move(vb),object_kinds[p],max_generation(p));if(!*history_out[h])return (cp.failure_line=26,fail(Error::InvalidArgument));}
 {auto pointers=world_pointers(world,v.ownership,v.geometry);if(!(symbols?add_symbolic_pointers(ms,std::move(pointers),*symbols):add_pointers(ms,std::move(pointers))))return (cp.failure_line=27,fail(symbols?Error::PermissionDenied:Error::InvalidArgument));}cp.bindings=ms.pointer_bindings;m.bindings=ms.pointers.get();m.world0=ev.world_id;
 Copier copier(cp);if(auto s=visit_world(world,m,allocator,copier);!s)return (cp.failure_line=28,fail(s.error()));
 for(auto*g:{&cp.sets,&cp.bodies,&cp.states,&cp.islands,&cp.cold_bodies,&cp.cold_contacts,&cp.cold_joints,&cp.set_contacts,&cp.set_joints,&cp.set_payloads,&cp.graph_contacts,&cp.graph_joints,&cp.graph_payloads,&cp.shapes,&cp.shape_geometry,&cp.chains,&cp.sensors})sort_records(*g);
 auto digest=digest_world(world,m,allocator);if(!digest)return (cp.failure_line=29,fail(digest.error()));cp.source_digest=*digest;
 if(persistent){++persistent->captures;persistent->last=stats;}
 return {};
}
Result<OwnedWorldEvents> restore_checkpoint(const WorldCheckpoint&cp,uint16_t world0,Allocator&allocator,MapStorage&ms){
 ms.bindings.reserve(64);ms.orders.reserve(64);ms.versioned.reserve(16);std::array<PoolCandidateInput,SP_POOL_COUNT> inputs{};for(uint32_t p=0;p<SP_POOL_COUNT;++p)inputs[p]={cp.pool_occupants[p],&cp.pool_records[p]};
 auto islands=restore_owned_islands(inputs,SolverBodyRecords{cp.sets,cp.bodies,cp.states},cp.islands,allocator);if(!islands)return (ms.failure_line=30,fail(islands.error()));
 auto graph=restore_owned_constraint_graph(std::move(*islands),OwnedGraphRecords{&cp.graph,cp.colors,cp.cold_bodies,cp.graph_contacts,cp.graph_joints,cp.graph_payloads},allocator);if(!graph)return (ms.failure_line=31,fail(graph.error()));
 ms.pointer_bindings=cp.bindings;ms.pointer_order.assign(cp.bindings.size()+1,0);{auto pm=PointerIdentityMap::prepare(ms.pointer_bindings,ms.pointer_order,binding_kind);if(!pm)return (ms.failure_line=36,fail(pm.error()));ms.pointers=std::make_unique<PointerIdentityMap>(*pm);}
 auto constraints=restore_owned_solver_constraints(std::move(*graph),OwnedConstraintRecords{cp.cold_contacts,cp.cold_joints,cp.set_contacts,cp.set_joints,cp.set_payloads},allocator,ms.pointers.get());if(!constraints)return (ms.failure_line=32,fail(constraints.error()));
 auto sets=assemble_owned_solver_sets(std::move(*constraints),allocator);if(!sets)return (ms.failure_line=33,fail(sets.error()));
 WorldDigestMaps&m=ms.digest;assign_singleton_ids(m);
 {std::vector<NativeBinding> mb;if(cp.events[0].fields.size()>1)for(size_t i=0;i<cp.events[0].fields[1].atoms.size();++i)mb.push_back({uint32_t(i),cp.events[0].fields[1].atoms[i].identity});m.move_events=add_map(ms,std::move(mb),body_move_kind);if(!m.move_events)return (ms.failure_line=34,fail(Error::InvalidArgument));}
 auto world=restore_owned_solver_world(std::move(*sets),cp.cold_bodies,*m.move_events,allocator,ms.pointers.get());if(!world)return (ms.failure_line=35,fail(world.error()));
 m.bindings=ms.pointers.get();
 auto root=restore_owned_world_root(std::move(*world),cp.root,*ms.pointers,allocator);if(!root)return (ms.failure_line=37,fail(root.error()));
 const auto&pools=root->solver_world().sets().constraints().graph().islands().solver_bodies().pools();
 std::array<const VersionedIdentityMap**,4> history_out{&m.body_history,&m.contact_history,&m.joint_history,&m.shape_history};const uint32_t history_pools[4]={SP_POOL_BODY,SP_POOL_CONTACT,SP_POOL_JOINT,SP_POOL_SHAPE};
 for(size_t h=0;h<4;++h){const uint32_t p=history_pools[h];const auto*pool=pools.pool(SpWorldPool(p));std::vector<VersionedNativeBinding> vb;for(uint32_t i=0;i<pool->pool().allocated_count;++i)if(auto id=pool->object_map().canonical(i))vb.push_back({{i,pool->native_generations()[i]},*id});
  for(const auto&r:cp.retired){if(r.pool!=p)continue;auto slot=pool->slot_map().native(r.slot);if(!slot)return (ms.failure_line=38,fail(slot.error()));vb.push_back({{*slot,r.generation},r.identity});}
  *history_out[h]=add_versioned(ms,std::move(vb),object_kinds[p],max_generation(p));if(!*history_out[h])return (ms.failure_line=39,fail(Error::InvalidArgument));}
 for(size_t t=0;t<3;++t){if(cp.trees[t].fields.size()<3||cp.trees[t].fields[2].atoms.empty())return (ms.failure_line=40,fail(Error::IncompatibleSchema));m.nodes[t]=node_map(ms,t,size_t(cp.trees[t].fields[2].atoms[0].bits));if(!m.nodes[t])return (ms.failure_line=41,fail(Error::InvalidArgument));}
 GeometryRecords gr{cp.shapes,cp.shape_geometry,cp.chains,cp.sensors,{&cp.trees[0],&cp.trees[1],&cp.trees[2]},{std::span<const Record>(cp.nodes[0]),std::span<const Record>(cp.nodes[1]),std::span<const Record>(cp.nodes[2])},&cp.moves,&cp.pairs,&cp.broadphase};
 GeometryMaps gm{m.nodes,m.shape_history,ms.pointers.get(),m.broadphase_ids};
 auto geo=restore_owned_geometry(std::move(*root),gr,gm,allocator);if(!geo)return (ms.failure_line=42,fail(geo.error()));
 m.world0=world0;const EventMaps em{*m.body_history,*m.shape_history,*m.contact_history,*m.joint_history,*ms.pointers,world0};
 WorldEventRecords er{&cp.events[0],&cp.events[1],&cp.events[2],&cp.events[7],&cp.events[8],{&cp.events[3],&cp.events[4]},{&cp.events[5],&cp.events[6]}};
 auto events=restore_owned_events(std::move(*geo),er,em,allocator);if(!events)return (ms.failure_line=43,fail(events.error()));
 return events;
}
void bind_candidate_maps(const OwnedWorldEvents&events,MapStorage&ms){WorldDigestMaps&m=ms.digest;
 const auto&pools=events.geometry_root().solver_root().solver_world().sets().constraints().graph().islands().solver_bodies().pools();
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){m.objects[p]=&pools.pool(SpWorldPool(p))->object_map();m.slots[p]=&pools.pool(SpWorldPool(p))->slot_map();}
 for(size_t t=0;t<3;++t)m.proxies[t]=&events.geometry_root().proxy_map(t);m.sensors=&events.geometry_root().sensor_map();m.move_events=&events.move_event_map();
}
bool destination_sources(const WorldCheckpoint&cp,const OwnedWorldEvents&events,SlotTranslation&out){
 const auto&pools=events.geometry_root().solver_root().solver_world().sets().constraints().graph().islands().solver_bodies().pools();
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){const auto*pool=pools.pool(SpWorldPool(p));if(!pool)return false;out[p].assign(pool->pool().allocated_count,0);
  for(uint32_t j=0;j<pool->pool().allocated_count;++j){auto id=pool->slot_map().canonical(j);if(!id||id->simulation<cp.slot_base[p])return false;const uint64_t k=id->simulation-cp.slot_base[p];if(k>=cp.slot_source[p].size())return false;out[p][j]=cp.slot_source[p][size_t(k)];}}
 return true;
}
bool slot_digest_maps(const void*world,MapStorage&ms){return slot_digest_maps(world,ms,nullptr);}
bool slot_digest_maps(const void*world,MapStorage&ms,const SlotTranslation*canonical){return slot_digest_maps(world,ms,canonical,nullptr);}
bool slot_digest_maps(const void*world,MapStorage&ms,const SlotTranslation*canonical,const SymbolRegistry*symbols){
 SpWorldCaptureView v{};if(!spCaptureWorldStructure(world,&v))return false;ms.bindings.reserve(64);ms.orders.reserve(64);ms.versioned.reserve(16);WorldDigestMaps&m=ms.digest;assign_singleton_ids(m);
 auto tr=[&](uint32_t p,uint32_t slot){return canonical&&slot<(*canonical)[p].size()?(*canonical)[p][slot]:slot;};
 auto id_of=[&](uint32_t p,uint32_t slot,uint32_t generation){return Identity{object_kinds[p],(uint64_t(tr(p,slot))+1)<<32|uint64_t(generation),1};};
 std::array<std::vector<std::pair<uint32_t,uint32_t>>,SP_POOL_COUNT> live;
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){auto d=pool_occupant_definition(SpWorldPool(p));if(!d)return false;std::vector<NativeBinding> ob,sb;for(uint32_t i=0;i<v.pools[p].allocated_count;++i){SpNativeLifetime n{};if(!spObserveWorldCaptureSlot(&v,SpWorldPool(p),i,&n))return false;sb.push_back({i,{d->slot_kind,uint64_t(tr(p,i))+1,1}});if(n.native_id!=-1){ob.push_back({i,id_of(p,i,n.generation)});live[p].push_back({i,n.generation});}}
  m.objects[p]=add_map(ms,std::move(ob),object_kinds[p]);m.slots[p]=add_map(ms,std::move(sb),d->slot_kind);if(!m.objects[p]||!m.slots[p])return false;}
 SpBroadPhaseView bp{};if(!spExportBroadPhase(v.geometry.broadphase,&bp))return false;
 for(uint32_t t=0;t<3;++t){const size_t cap=size_t(std::max(bp.trees[t].nodeCapacity,0));m.nodes[t]=node_map(ms,t,cap);std::vector<NativeBinding> pb;for(size_t i=0;i<cap;++i){const auto&n=bp.trees[t].nodes[i];if(!(n.flags&b2_allocatedNode)||!(n.flags&b2_leafNode))continue;auto shape=m.objects[SP_POOL_SHAPE]->canonical(uint32_t(n.userData));if(!shape)return false;pb.push_back({uint32_t(i),{shape_proxy_kind,shape->simulation,shape->generation}});}m.proxies[t]=add_map(ms,std::move(pb),shape_proxy_kind);if(!m.nodes[t]||!m.proxies[t])return false;}
 {const auto geo=spGeometryLayout();std::vector<SpVisitor> a(v.geometry.shape_count+8),b(a.size()),c(a.size());std::vector<NativeBinding> sb;for(uint32_t i=0;i<v.geometry.sensor_count;++i){SpSensorView s{};s.hits=a.data();s.overlaps1=b.data();s.overlaps2=c.data();s.hit_capacity=s.first_capacity=s.second_capacity=uint32_t(a.size());if(!spExportSensor(static_cast<const std::byte*>(v.geometry.sensors)+size_t(i)*geo.sensor_size,&s))return false;auto shape=m.objects[SP_POOL_SHAPE]->canonical(uint32_t(s.shape));if(!shape)return false;sb.push_back({i,{sensor_kind,shape->simulation,shape->generation}});}m.sensors=add_map(ms,std::move(sb),sensor_kind);}
 SpWorldEventArrays ev{};if(!spExportWorldEvents(world,&ev))return false;{std::vector<NativeBinding> mb;for(uint32_t i=0;i<ev.move.count;++i)mb.push_back({i,{body_move_kind,uint64_t(i)+1,1}});m.move_events=add_map(ms,std::move(mb),body_move_kind);}
 const auto named=named_lifetimes(world,v.geometry);std::array<const VersionedIdentityMap**,4> history_out{&m.body_history,&m.contact_history,&m.joint_history,&m.shape_history};const uint32_t history_pools[4]={SP_POOL_BODY,SP_POOL_CONTACT,SP_POOL_JOINT,SP_POOL_SHAPE};
 for(size_t h=0;h<4;++h){const uint32_t p=history_pools[h];std::vector<VersionedNativeBinding> vb;for(auto[slot,g]:live[p])vb.push_back({{slot,g},id_of(p,slot,g)});for(const auto&r:named)if(r.pool==p&&!std::binary_search(live[p].begin(),live[p].end(),std::pair<uint32_t,uint32_t>{r.slot,r.generation}))vb.push_back({{r.slot,r.generation},id_of(p,r.slot,r.generation)});
  *history_out[h]=add_versioned(ms,std::move(vb),object_kinds[p],max_generation(p));if(!*history_out[h])return false;}
 {auto pointers=world_pointers(world,v.ownership,v.geometry);if(!(symbols?add_symbolic_pointers(ms,std::move(pointers),*symbols):add_pointers(ms,std::move(pointers))))return false;}m.bindings=ms.pointers.get();m.world0=ev.world_id;return true;
}
}
