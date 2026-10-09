// SPDX-License-Identifier: MIT
#include "dynamic_tree_codec.hpp"
#include <algorithm>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,7> defs{{
 {1,AtomType::Reference,tree_node_kind,1,1,true},{2,AtomType::Unsigned,0,1,1,false},
 {3,AtomType::Unsigned,0,1,1,false},{4,AtomType::Reference,tree_node_kind,1,1,true},
 {5,AtomType::Unsigned,0,1,1,false},{6,AtomType::Unsigned,0,1,1,false},
 {7,AtomType::Reference,tree_node_kind,0,100000,false}}};
struct Range{const void *p;size_t n;};
bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.n&&b.n&&(x<=y?y-x<a.n:x-y<b.n);}
template<class T>Range range(std::span<T> s)noexcept{return {s.data(),s.size_bytes()};}
Status disjoint(std::span<const Range> a)noexcept{for(size_t i=0;i<a.size();++i)for(size_t j=i+1;j<a.size();++j)if(overlap(a[i],a[j]))return fail(Error::InvalidArgument);return {};}
Status boundary(TreeCaptureBoundary b)noexcept{return b.physics_jobs_drained&&b.rebuild_drained&&b.heuristic==0?Status{}:Status(fail(Error::NotReady));}
Status complete_map(const IdentityMap &map,size_t count)noexcept{for(uint32_t i=0;i<count;++i){auto x=map.canonical(i);if(!x||x->kind!=tree_node_kind)return fail(Error::StaleGeneration);}return {};}
Result<int> native_ref(Identity id,const IdentityMap &map)noexcept{if(!id.kind){if(id.simulation||id.generation)return fail(Error::InvalidArgument);return -1;}if(id.kind!=tree_node_kind)return fail(Error::IncompatibleSchema);auto n=map.native(id);if(!n)return fail(n.error());if(*n>100000)return fail(Error::InvalidArgument);return int(*n);}
}
DynamicTreeImage::DynamicTreeImage(std::span<Atom> storage)noexcept:node_storage(storage){for(size_t i=0;i<6;++i)fields[i]={defs[i].id,std::span(atoms).subspan(i,1)};fields[6]={7,{}};record.fields=fields;}
std::span<const FieldSpec> dynamic_tree_fields()noexcept{return defs;}
Status capture_dynamic_tree(const b2DynamicTree &n,Identity id,const IdentityMap &map,TreeCaptureBoundary b,std::span<uint8_t> marks,std::span<uint32_t> stack,DynamicTreeImage &out)noexcept{
 if(auto s=boundary(b);!s)return s;
 if(id.kind!=dynamic_tree_kind||!id.simulation||!id.generation||n.nodeCapacity<0||n.nodeCapacity>100000||n.nodeCount<0||n.proxyCount<0||n.rebuildCapacity<0||n.rebuildCapacity>100000||(!n.nodes&&n.nodeCapacity)||(!n.leafIndices&&n.rebuildCapacity)||(!n.leafCenters&&n.rebuildCapacity)||n.leafBoxes||n.binIndices)return fail(Error::InvalidArgument);
 auto nodes=std::span(n.nodes,size_t(n.nodeCapacity));if(out.node_storage.size()<nodes.size())return fail(Error::CapacityExceeded);
 const std::array<Range,8> ranges{{range(nodes),range(marks),range(stack),range(out.node_storage),{&out,sizeof(out)},{&n,sizeof(n)},range(std::span(n.leafIndices,size_t(n.rebuildCapacity))),range(std::span(n.leafCenters,size_t(n.rebuildCapacity)))}};if(auto s=disjoint(ranges);!s)return s;
 if(auto s=complete_map(map,nodes.size());!s)return s;if(auto s=validate_tree_topology(nodes,n.root,n.freeList,uint32_t(n.nodeCount),uint32_t(n.proxyCount),marks,stack);!s)return s;
 std::array<Atom,6> atoms{};for(auto i:{0,3}){int value=i==0?n.root:n.freeList;if(value!=-1){auto r=map.canonical(uint32_t(value));if(!r)return fail(r.error());atoms[i].identity=*r;}}
 atoms[1].bits=uint32_t(n.nodeCount);atoms[2].bits=uint32_t(n.nodeCapacity);atoms[4].bits=uint32_t(n.proxyCount);atoms[5].bits=uint32_t(n.rebuildCapacity);
 for(uint32_t i=0;i<nodes.size();++i){auto r=map.canonical(i);out.node_storage[i]={0,*r};}
 auto entries=out.node_storage.first(nodes.size());std::sort(entries.begin(),entries.end(),[](const Atom&a,const Atom&b){return a.identity.simulation<b.identity.simulation;});out.atoms=atoms;out.fields[6].atoms=entries;out.record.identity=id;return {};
}
Result<b2DynamicTree> restore_dynamic_tree(const Record &r,std::span<const Record> records,const IdentityMap &map,const IdentityMap &shapes,TreeCaptureBoundary b,DynamicTreeStorage dst,std::span<uint8_t> marks,std::span<uint32_t> stack)noexcept{
 if(auto s=boundary(b);!s)return fail(s.error());if(r.identity.kind!=dynamic_tree_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=7)return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<7;++i){if(r.fields[i].id!=defs[i].id||r.fields[i].atoms.size()<(i==6?0:1)||r.fields[i].atoms.size()>(i==6?100000:1))return fail(Error::IncompatibleSchema);}
 for(auto i:{1,2,4,5}){const auto&a=r.fields[i].atoms[0];if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>100000)return fail(Error::InvalidArgument);}
 for(auto i:{0,3})if(r.fields[i].atoms[0].bits)return fail(Error::InvalidArgument);
 auto count=size_t(r.fields[2].atoms[0].bits),rebuild=size_t(r.fields[5].atoms[0].bits);auto entries=r.fields[6].atoms;
 if(dst.nodes.size()!=count||dst.leaf_indices.size()!=rebuild||dst.leaf_centers.size()!=rebuild||entries.size()!=count||records.size()!=count)return fail(Error::CapacityExceeded);
 const std::array<Range,7> outputs{{range(dst.nodes),range(dst.leaf_indices),range(dst.leaf_centers),range(marks),range(stack),{&r,sizeof(r)},range(records)}};if(auto s=disjoint(outputs);!s)return fail(s.error());
 for(size_t i=0;i<5;++i)if(overlap(outputs[i],range(r.fields)))return fail(Error::InvalidArgument);
 for(const auto &field:r.fields){for(size_t i=0;i<5;++i)if(overlap(outputs[i],range(field.atoms)))return fail(Error::InvalidArgument);}
 for(const auto &record:records){for(size_t i=0;i<5;++i){if(overlap(outputs[i],range(record.fields)))return fail(Error::InvalidArgument);for(const auto &f:record.fields)if(overlap(outputs[i],range(f.atoms)))return fail(Error::InvalidArgument);}}
 if(auto s=complete_map(map,count);!s)return fail(s.error());auto root=native_ref(r.fields[0].atoms[0].identity,map),free=native_ref(r.fields[3].atoms[0].identity,map);if(!root)return fail(root.error());if(!free)return fail(free.error());
 for(size_t i=0;i<count;++i){const auto&a=entries[i];if(a.bits||a.identity.kind!=tree_node_kind||(i&&entries[i-1].identity.simulation>=a.identity.simulation)||records[i].identity!=a.identity)return fail(Error::NonCanonical);auto n=map.native(a.identity);if(!n)return fail(n.error());if(*n>=count)return fail(Error::InvalidArgument);if(auto s=restore_tree_node(records[i],map,shapes,dst.nodes[*n]);!s)return fail(s.error());}
 if(auto s=validate_tree_topology(dst.nodes,*root,*free,uint32_t(r.fields[1].atoms[0].bits),uint32_t(r.fields[4].atoms[0].bits),marks,stack);!s)return fail(s.error());
 std::fill(dst.leaf_indices.begin(),dst.leaf_indices.end(),0);std::fill(dst.leaf_centers.begin(),dst.leaf_centers.end(),b2Vec2{});
 b2DynamicTree result{};result.nodes=dst.nodes.data();result.root=*root;result.nodeCount=int(r.fields[1].atoms[0].bits);result.nodeCapacity=int(count);result.freeList=*free;result.proxyCount=int(r.fields[4].atoms[0].bits);result.leafIndices=dst.leaf_indices.data();result.leafCenters=dst.leaf_centers.data();result.rebuildCapacity=int(rebuild);return result;
}
}
