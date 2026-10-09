// SPDX-License-Identifier: MIT
#include "geometry_root.hpp"
#include "runtime_scratch_bridge.h"
#include <algorithm>
#include <memory>
#include <utility>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Layout{size_t bytes{};bool valid=true;size_t raw(size_t n,size_t s,size_t a)noexcept{if(!valid||!s||!a||(a&(a-1))||n>SIZE_MAX/s){valid=false;return 0;}size_t pad=(a-bytes%a)%a;if(bytes>SIZE_MAX-pad||bytes+pad>SIZE_MAX-n*s){valid=false;return 0;}bytes+=pad;size_t offset=bytes;bytes+=n*s;return offset;}template<class T>size_t add(size_t n)noexcept{return raw(n,sizeof(T),alignof(T));}};
void*raw_take(void*p,Layout&l,size_t n,size_t s,size_t a)noexcept{return static_cast<std::byte*>(p)+l.raw(n,s,a);}
template<class T>std::span<T>take(void*p,Layout&l,size_t n)noexcept{auto*q=reinterpret_cast<T*>(static_cast<std::byte*>(p)+l.add<T>(n));for(size_t i=0;i<n;++i)std::construct_at(q+i);return {q,n};}
bool less(Identity a,Identity b)noexcept{return a.kind<b.kind||(a.kind==b.kind&&a.simulation<b.simulation);}
bool sorted(std::span<const Record>rs,uint32_t kind=0)noexcept{if(rs.size()>100000)return false;for(size_t i=0;i<rs.size();++i)if((kind&&rs[i].identity.kind!=kind)||!rs[i].identity.kind||!rs[i].identity.simulation||!rs[i].identity.generation||(i&&!less(rs[i-1].identity,rs[i].identity)))return false;return true;}
const Record*find(std::span<const Record>rs,Identity id)noexcept{auto it=std::lower_bound(rs.begin(),rs.end(),id,[](const Record&r,Identity v){return less(r.identity,v);});return it!=rs.end()&&it->identity==id?&*it:nullptr;}
bool bounded(const Record*r,size_t field,size_t atom,uint64_t limit)noexcept{return r&&r->fields.size()>field&&r->fields[field].atoms.size()>atom&&r->fields[field].atoms[atom].bits<=limit;}
uint64_t bits(const Record&r,size_t field,size_t atom=0)noexcept{return r.fields[field].atoms[atom].bits;}
constexpr uint32_t geometry_kinds[5]={circle_geometry_kind,capsule_geometry_kind,segment_geometry_kind,polygon_geometry_kind,chain_segment_geometry_kind};
}
OwnedGeometryRoot::OwnedGeometryRoot(OwnedGeometryRoot&&o)noexcept:allocator_(o.allocator_),block_(std::exchange(o.block_,nullptr)),bytes_(std::exchange(o.bytes_,0)),world_(std::exchange(o.world_,nullptr)),ownership_(std::exchange(o.ownership_,nullptr)),geometry_(std::exchange(o.geometry_,nullptr)),proxies_(o.proxies_),sensors_(o.sensors_){
 for(auto&p:o.proxies_)p.reset();o.sensors_.reset();if(o.prior_){prior_.emplace(std::move(*o.prior_));o.prior_.reset();}}
OwnedGeometryRoot::~OwnedGeometryRoot(){if(block_)allocator_->deallocate(block_);}
Result<OwnedGeometryRoot>restore_owned_geometry(OwnedWorldRoot&&prior,const GeometryRecords&in,const GeometryMaps&maps,Allocator&allocator)noexcept{
 if(!prior.has_storage()||!maps.history||!maps.bindings||!in.moves||!in.pairs||!in.broadphase)return fail(Error::InvalidArgument);
 for(size_t t=0;t<3;++t)if(!maps.nodes[t]||!in.trees[t])return fail(Error::InvalidArgument);
 const auto&pools=prior.solver_world().sets().constraints().graph().islands().solver_bodies().pools();
 const auto*sp=pools.pool(SP_POOL_SHAPE),*cp=pools.pool(SP_POOL_CHAIN);if(!sp||!cp)return fail(Error::InvalidArgument);
 const auto&shape_map=sp->object_map();const auto&chain_map=cp->object_map();const auto&body_map=pools.pool(SP_POOL_BODY)->object_map();
 const size_t sc=sp->pool().allocated_count,chc=cp->pool().allocated_count,ns=in.sensors.size();
 if(sc>100000||chc>100000||!sorted(in.shapes,shape_kind)||!sorted(in.shape_geometry)||!sorted(in.chains,chain_kind)||!sorted(in.sensors,sensor_kind)||in.shapes.size()!=sc-sp->pool().free_count||in.chains.size()!=chc-cp->pool().free_count||in.shape_geometry.size()!=in.shapes.size())return fail(Error::InvalidArgument);
 // Bounded storage sizes read from the records; codecs validate them fully.
 std::array<size_t,3> tree_nodes{},tree_rebuild{},bit_caps{};size_t total_nodes=0,max_nodes=0;
 for(size_t t=0;t<3;++t){if(!bounded(in.trees[t],2,0,100000)||!bounded(in.trees[t],5,0,100000)||in.nodes[t].size()>100000)return fail(Error::IncompatibleSchema);tree_nodes[t]=size_t(bits(*in.trees[t],2));tree_rebuild[t]=size_t(bits(*in.trees[t],5));total_nodes+=tree_nodes[t];max_nodes=std::max(max_nodes,tree_nodes[t]);}
 if(!bounded(in.moves,0,0,100000)||in.moves->fields.size()!=5||in.moves->fields[1].atoms.size()!=3)return fail(Error::IncompatibleSchema);
 const size_t move_capacity=size_t(bits(*in.moves,0)),move_refs=in.moves->fields[3].atoms.size();for(size_t t=0;t<3;++t){if(!bounded(in.moves,1,t,1563))return fail(Error::IncompatibleSchema);bit_caps[t]=size_t(bits(*in.moves,1,t));}
 if(!bounded(in.pairs,0,0,131072)||in.pairs->fields.size()!=2)return fail(Error::IncompatibleSchema);const size_t pair_capacity=size_t(bits(*in.pairs,0)),pair_count=in.pairs->fields[1].atoms.size()/2;
 size_t chain_shapes=0,chain_materials=0,max_chain=0,visitors=0,max_visitors=0;
 for(const auto&r:in.chains){if(r.fields.size()<6)return fail(Error::IncompatibleSchema);chain_shapes+=r.fields[4].atoms.size();chain_materials+=r.fields[5].atoms.size();max_chain=std::max(max_chain,r.fields[4].atoms.size());}
 for(const auto&r:in.sensors){if(r.fields.size()<7)return fail(Error::IncompatibleSchema);size_t own=0;for(size_t c=1;c<4;++c){if(!bounded(&r,c,0,100000))return fail(Error::IncompatibleSchema);own+=size_t(bits(r,c));}visitors+=own;max_visitors=std::max(max_visitors,own);}
 if(chain_shapes>100000||chain_materials>100000||visitors>100000)return fail(Error::CapacityExceeded);
 const auto root=spWorldRootLayout();const auto geo=spGeometryLayout();const size_t contacts=prior.solver_world().ownership_view().contact_count,key_capacity=std::max(pair_count,contacts);
 Layout plan;plan.raw(1,root.size,root.alignment);plan.add<SpWorldOwnershipView>(1);plan.add<SpGeometryOwnershipView>(1);plan.add<b2Shape>(sc);plan.raw(chc,geo.chain_size,geo.chain_alignment);plan.add<int>(chain_shapes);plan.add<b2SurfaceMaterial>(chain_materials);plan.add<Identity>(max_chain);
 plan.raw(ns,geo.sensor_size,geo.sensor_alignment);plan.raw(visitors,geo.visitor_size,geo.visitor_alignment);plan.add<SpVisitor>(visitors);plan.add<SpVisitor>(max_visitors);plan.add<NativeBinding>(ns);plan.add<uint32_t>(ns);
 for(size_t t=0;t<3;++t){plan.add<b2TreeNode>(tree_nodes[t]);plan.add<int32_t>(tree_rebuild[t]);plan.add<b2Vec2>(tree_rebuild[t]);plan.add<NativeBinding>(tree_nodes[t]);plan.add<uint32_t>(tree_nodes[t]);plan.add<uint64_t>(bit_caps[t]);}
 plan.add<uint8_t>(max_nodes);plan.add<uint32_t>(max_nodes);plan.add<int>(move_capacity);plan.add<uint32_t>(move_refs);plan.add<b2SetItem>(pair_capacity);plan.add<uint64_t>(pair_count);
 plan.add<uint8_t>(total_nodes);plan.add<uint32_t>(std::max(total_nodes,move_refs));plan.add<uint64_t>(total_nodes+pair_count);plan.add<uint8_t>(sc);plan.add<uint64_t>(key_capacity);plan.add<uint64_t>(key_capacity);
 if(!plan.valid)return fail(Error::CapacityExceeded);
 OwnedGeometryRoot out;out.allocator_=&allocator;out.bytes_=std::max(plan.bytes,size_t(1));out.block_=allocator.allocate(out.bytes_,std::max({alignof(std::max_align_t),size_t(root.alignment),size_t(geo.chain_alignment),size_t(geo.sensor_alignment),alignof(b2Shape)}),MemoryDomain::Recovery);if(!out.block_)return fail(Error::OutOfMemory);
 Layout c;out.world_=raw_take(out.block_,c,1,root.size,root.alignment);auto ownership=take<SpWorldOwnershipView>(out.block_,c,1);auto geometry=take<SpGeometryOwnershipView>(out.block_,c,1);auto shapes=take<b2Shape>(out.block_,c,sc);
 auto*chains=static_cast<std::byte*>(raw_take(out.block_,c,chc,geo.chain_size,geo.chain_alignment));auto chain_shape_storage=take<int>(out.block_,c,chain_shapes);auto chain_material_storage=take<b2SurfaceMaterial>(out.block_,c,chain_materials);auto chain_scratch=take<Identity>(out.block_,c,max_chain);
 auto*sensors=static_cast<std::byte*>(raw_take(out.block_,c,ns,geo.sensor_size,geo.sensor_alignment));auto*native_visitors=static_cast<std::byte*>(raw_take(out.block_,c,visitors,geo.visitor_size,geo.visitor_alignment));auto visitor_views=take<SpVisitor>(out.block_,c,visitors);auto visitor_scratch=take<SpVisitor>(out.block_,c,max_visitors);auto sensor_bindings=take<NativeBinding>(out.block_,c,ns);auto sensor_order=take<uint32_t>(out.block_,c,ns);
 std::array<std::span<b2TreeNode>,3> nodes;std::array<std::span<int32_t>,3> leaf_indices;std::array<std::span<b2Vec2>,3> leaf_centers;std::array<std::span<NativeBinding>,3> proxy_bindings;std::array<std::span<uint32_t>,3> proxy_order;std::array<std::span<uint64_t>,3> move_bits;
 for(size_t t=0;t<3;++t){nodes[t]=take<b2TreeNode>(out.block_,c,tree_nodes[t]);leaf_indices[t]=take<int32_t>(out.block_,c,tree_rebuild[t]);leaf_centers[t]=take<b2Vec2>(out.block_,c,tree_rebuild[t]);proxy_bindings[t]=take<NativeBinding>(out.block_,c,tree_nodes[t]);proxy_order[t]=take<uint32_t>(out.block_,c,tree_nodes[t]);move_bits[t]=take<uint64_t>(out.block_,c,bit_caps[t]);}
 auto tree_marks=take<uint8_t>(out.block_,c,max_nodes);auto tree_stack=take<uint32_t>(out.block_,c,max_nodes);auto moves=take<int>(out.block_,c,move_capacity);auto move_order=take<uint32_t>(out.block_,c,move_refs);auto pair_items=take<b2SetItem>(out.block_,c,pair_capacity);auto pair_keys=take<uint64_t>(out.block_,c,pair_count);
 auto bp_marks=take<uint8_t>(out.block_,c,total_nodes);auto bp_order=take<uint32_t>(out.block_,c,std::max(total_nodes,move_refs));auto bp_ids=take<uint64_t>(out.block_,c,total_nodes+pair_count);auto geo_marks=take<uint8_t>(out.block_,c,sc);auto contact_keys=take<uint64_t>(out.block_,c,key_capacity),pair_witness=take<uint64_t>(out.block_,c,key_capacity);
 if(c.bytes!=plan.bytes)return fail(Error::InvalidArgument);
 // Broadphase: trees (destination node-slot maps), leaf proxies, moves, pairs.
 const TreeCaptureBoundary boundary{true,true,0};BroadPhaseParts parts{};parts.ids=maps.ids;
 for(size_t t=0;t<3;++t){auto tree=restore_dynamic_tree(*in.trees[t],in.nodes[t],*maps.nodes[t],shape_map,boundary,DynamicTreeStorage{nodes[t],leaf_indices[t],leaf_centers[t]},tree_marks.first(tree_nodes[t]),tree_stack.first(tree_nodes[t]));if(!tree)return fail(tree.error());parts.trees[t]=*tree;
  size_t leaves=0;for(size_t i=0;i<tree_nodes[t];++i){const auto&n=nodes[t][i];if(!(n.flags&b2_allocatedNode)||!(n.flags&b2_leafNode))continue;if(n.userData>UINT32_MAX)return fail(Error::InvalidArgument);auto shape=shape_map.canonical(uint32_t(n.userData));if(!shape)return fail(shape.error());proxy_bindings[t][leaves++]={uint32_t(i),{shape_proxy_kind,shape->simulation,shape->generation}};}
  auto map=IdentityMap::prepare(proxy_bindings[t].first(leaves),proxy_order[t],shape_proxy_kind);if(!map)return fail(map.error());out.proxies_[t].emplace(*map);}
 const std::array<const IdentityMap*,3> proxies{&*out.proxies_[0],&*out.proxies_[1],&*out.proxies_[2]};
 auto restored_moves=restore_moves(*in.moves,MoveMappings{proxies},move_order,MoveStorage{{move_bits[0],move_bits[1],move_bits[2]},moves});if(!restored_moves)return fail(restored_moves.error());parts.moves=*restored_moves;
 auto restored_pairs=restore_pair_set(*in.pairs,shape_map,pair_keys,pair_items);if(!restored_pairs)return fail(restored_pairs.error());parts.pairs=*restored_pairs;
 auto broadphase=restore_broadphase(*in.broadphase,parts,BroadPhaseMaps{maps.nodes,proxies,&shape_map},boundary,BroadPhaseScratch{bp_marks,bp_order,bp_ids});if(!broadphase)return fail(broadphase.error());
 if(!spCopyWorldRoot(out.world_,prior.native_world())||!spImportBroadPhaseCandidate(&*broadphase,spWorldRootBroadPhase(out.world_)))return fail(Error::InvalidArgument);
 // Dense sensors in canonical (admitted native) order.
 for(size_t i=0;i<ns;++i)sensor_bindings[i]={uint32_t(i),in.sensors[i].identity};auto sensor_map=IdentityMap::prepare(sensor_bindings,sensor_order,sensor_kind);if(!sensor_map)return fail(sensor_map.error());out.sensors_.emplace(*sensor_map);
 // Shapes with their tagged geometry child.
 const ShapeMappings shape_maps{shape_map,body_map,*out.sensors_,chain_map,*maps.bindings,proxies};
 for(const auto&r:in.shapes){auto id=shape_map.native(r.identity);if(!id||*id>=sc||shapes[*id].generation)return fail(Error::StaleGeneration);const Record*child=nullptr;for(auto k:geometry_kinds)if(!child)child=find(in.shape_geometry,{k,r.identity.simulation,r.identity.generation});if(!child)return fail(Error::StaleGeneration);
  if(auto s=restore_shape(r,*child,shape_maps,shapes[*id]);!s)return fail(s.error());if(shapes[*id].id!=int(*id)||shapes[*id].generation!=sp->native_generations()[*id]||!shapes[*id].generation)return fail(Error::InvalidArgument);}
 for(size_t id=0;id<sc;++id)if(!shape_map.canonical(uint32_t(id))){if(sp->native_generations()[id]>UINT16_MAX)return fail(Error::InvalidArgument);auto&s=shapes[id];s=b2Shape{};s.id=s.bodyId=s.prevShapeId=s.nextShapeId=s.sensorIndex=s.proxyKey=-1;s.generation=uint16_t(sp->native_generations()[id]);}
 // Chains with exact segment/material arrays.
 size_t shape_cursor=0,material_cursor=0;for(size_t id=0;id<chc;++id)if(!spPrepareFreeChain(chains+id*geo.chain_size,cp->native_generations()[id]))return fail(Error::InvalidArgument);
 for(const auto&r:in.chains){auto id=chain_map.native(r.identity);if(!id||*id>=chc)return fail(Error::StaleGeneration);const size_t n=r.fields[4].atoms.size(),m=r.fields[5].atoms.size();int*ids=chain_shape_storage.data()+shape_cursor;b2SurfaceMaterial*mats=chain_material_storage.data()+material_cursor;shape_cursor+=n;material_cursor+=m;
  SpChainView view{};view.shapes=ids;view.materials=mats;view.shape_capacity=uint32_t(n);view.material_capacity=uint32_t(m);if(auto s=restore_chain(r,ChainMaps{chain_map,body_map,shape_map},chain_scratch,view);!s)return fail(s.error());
  void*native=chains+size_t(*id)*geo.chain_size;if(view.id!=int(*id)||view.generation!=cp->native_generations()[*id]||!spPrepareChainCandidate(native,ids,uint32_t(n),mats,uint32_t(m))||!spImportChainCandidate(&view,native))return fail(Error::InvalidArgument);}
 // Sensors with exact visitor capacities; empty hits at the capture barrier.
 size_t visitor_cursor=0;for(size_t i=0;i<ns;++i){const auto&r=in.sensors[i];const size_t hc=size_t(bits(r,1)),fc=size_t(bits(r,2)),scap=size_t(bits(r,3));SpVisitor*v=visitor_views.data()+visitor_cursor;std::byte*nv=native_visitors+visitor_cursor*geo.visitor_size;visitor_cursor+=hc+fc+scap;
  SpSensorView view{};view.hits=v;view.overlaps1=v+hc;view.overlaps2=v+hc+fc;view.hit_capacity=uint32_t(hc);view.first_capacity=uint32_t(fc);view.second_capacity=uint32_t(scap);if(auto s=restore_sensor(r,shape_map,*maps.history,visitor_scratch,view);!s)return fail(s.error());
  void*native=sensors+i*geo.sensor_size;if(!spPrepareSensorCandidate(native,nv,uint32_t(hc),nv+hc*geo.visitor_size,uint32_t(fc),nv+(hc+fc)*geo.visitor_size,uint32_t(scap))||!spImportSensorCandidate(&view,native))return fail(Error::InvalidArgument);}
 if(shape_cursor!=chain_shapes||material_cursor!=chain_materials||visitor_cursor!=visitors)return fail(Error::InvalidArgument);
 const SpPoolView shape_pool=sp->pool(),chain_pool=cp->pool();
 if(!spBindWorldRootGeometry(out.world_,shapes.data(),uint32_t(sc),chains,uint32_t(chc),sensors,uint32_t(ns),&shape_pool,&chain_pool)||!spValidateNormalizedRuntimeScratch(out.world_))return fail(Error::InvalidArgument);
 // Complete-root validation: solver ownership, all seven pools and geometry ownership.
 if(!spWorldRootOwnershipView(out.world_,&ownership[0])||!spValidateWorldOwnership(&ownership[0])||!spValidateWorldRootPools(out.world_)||!spValidateWorldRootGeometryPools(out.world_)||!spWorldRootGeometryView(out.world_,&ownership[0],&geometry[0]))return fail(Error::InvalidArgument);
 uint32_t witnessed=0;if(!spValidateGeometryOwnership(&geometry[0],geo_marks.data(),uint32_t(sc),contact_keys.data(),pair_witness.data(),uint32_t(key_capacity),&witnessed)||witnessed!=pair_count)return fail(Error::InvalidArgument);
 out.ownership_=&ownership[0];out.geometry_=&geometry[0];out.prior_.emplace(std::move(prior));return out;
}
}
