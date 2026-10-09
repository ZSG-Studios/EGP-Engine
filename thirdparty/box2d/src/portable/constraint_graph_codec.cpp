// SPDX-License-Identifier: MIT
#include "constraint_graph_codec.hpp"
#include <algorithm>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,1> defs{{{1,AtomType::Reference,graph_color_kind,24,24,false}}};
bool overlap(const void*a,size_t n,const void*b,size_t m)noexcept{auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return n&&m&&(x<=y?y-x<n:x-y<m);}
struct Range{const void*pointer;size_t bytes;};
bool mapping_overlap(const GraphColorMaps&m,Range r)noexcept{
 if(overlap(r.pointer,r.bytes,&m,sizeof(m))||overlap(r.pointer,r.bytes,m.roles.data(),m.roles.size_bytes())||overlap(r.pointer,r.bytes,m.contact_endpoints.data(),m.contact_endpoints.size_bytes()))return true;
 for(const auto*map:{&m.bodies,&m.contacts,&m.joints,&m.solver_bodies})if(map->overlaps_storage(r.pointer,r.bytes))return true;return false;
}
std::array<Range,3> buffers(const SpGraphColorView&c)noexcept{return {{{c.body_set.bits,size_t(c.body_set.blockCapacity)*sizeof(uint64_t)},{c.contacts,size_t(c.contact_count)*sizeof(SpContactSim)},{c.joints,size_t(c.joint_count)*sizeof(SpJointSim)}}};}
Status admission(std::span<const SpGraphColorView> colors,const GraphColorMaps&m,TreeCaptureBoundary boundary,ConstraintGraphScratch scratch)noexcept{
 if(!boundary.physics_jobs_drained||!boundary.rebuild_drained||boundary.heuristic)return fail(Error::NotReady);
 if(colors.size()!=24||spGraphColorCount()!=24)return fail(Error::IncompatibleSchema);
 size_t contacts=0,joints=0;for(const auto&c:colors){if(c.contact_count>100000-contacts||c.joint_count>100000-joints)return fail(Error::CapacityExceeded);contacts+=c.contact_count;joints+=c.joint_count;}if(contacts+joints>100000||scratch.identities.size()<contacts+joints)return fail(Error::CapacityExceeded);
 std::array<Range,5> work{{{scratch.color.data(),scratch.color.size_bytes()},{scratch.identities.data(),scratch.identities.size_bytes()},{scratch.color_atoms[0].data(),scratch.color_atoms[0].size_bytes()},{scratch.color_atoms[1].data(),scratch.color_atoms[1].size_bytes()},{scratch.color_atoms[2].data(),scratch.color_atoms[2].size_bytes()}}};
 for(size_t i=0;i<work.size();++i){if(mapping_overlap(m,work[i]))return fail(Error::InvalidArgument);for(size_t j=i+1;j<work.size();++j)if(overlap(work[i].pointer,work[i].bytes,work[j].pointer,work[j].bytes))return fail(Error::InvalidArgument);if(overlap(work[i].pointer,work[i].bytes,colors.data(),colors.size_bytes())||overlap(work[i].pointer,work[i].bytes,m.roles.data(),m.roles.size_bytes())||overlap(work[i].pointer,work[i].bytes,m.contact_endpoints.data(),m.contact_endpoints.size_bytes()))return fail(Error::InvalidArgument);}
 for(size_t i=0;i<colors.size();++i){auto bi=buffers(colors[i]);for(size_t a=0;a<bi.size();++a){for(size_t b=a+1;b<bi.size();++b)if(overlap(bi[a].pointer,bi[a].bytes,bi[b].pointer,bi[b].bytes))return fail(Error::InvalidArgument);for(auto w:work)if(overlap(bi[a].pointer,bi[a].bytes,w.pointer,w.bytes))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)for(auto bj:buffers(colors[j]))if(overlap(bi[a].pointer,bi[a].bytes,bj.pointer,bj.bytes))return fail(Error::InvalidArgument);}}
 size_t ci=0,ji=contacts;GraphColorImage temporary(scratch.color_atoms);
 for(size_t i=0;i<colors.size();++i){auto v=capture_graph_color(colors[i],uint32_t(i),{graph_color_kind,i+1,1},m,boundary,scratch.color,temporary);if(!v)return v;for(size_t j=0;j<colors[i].contact_count;++j)scratch.identities[ci++]=uint32_t(colors[i].contacts[j].contactId);for(size_t j=0;j<colors[i].joint_count;++j)scratch.identities[ji++]=uint32_t(colors[i].joints[j].jointId);}
 auto c=scratch.identities.first(contacts),j=scratch.identities.subspan(contacts,joints);std::sort(c.begin(),c.end());std::sort(j.begin(),j.end());for(size_t i=1;i<c.size();++i)if(c[i]==c[i-1])return fail(Error::InvalidArgument);for(size_t i=1;i<j.size();++i)if(j[i]==j[i-1])return fail(Error::InvalidArgument);return {};
}
bool same(const Record&a,const Record&b)noexcept{if(a.identity!=b.identity||a.fields.size()!=b.fields.size())return false;for(size_t f=0;f<a.fields.size();++f){if(a.fields[f].id!=b.fields[f].id||a.fields[f].atoms.size()!=b.fields[f].atoms.size())return false;for(size_t i=0;i<a.fields[f].atoms.size();++i){auto x=a.fields[f].atoms[i],y=b.fields[f].atoms[i];if(x.bits!=y.bits||x.identity!=y.identity)return false;}}return true;}
}
ConstraintGraphImage::ConstraintGraphImage()noexcept{field={1,atoms};record.fields=std::span(&field,1);}
std::span<const FieldSpec> constraint_graph_fields()noexcept{return defs;}
Status capture_constraint_graph(std::span<const SpGraphColorView>colors,std::span<const Identity>ids,Identity id,const GraphColorMaps&m,TreeCaptureBoundary boundary,ConstraintGraphScratch scratch,ConstraintGraphImage&out)noexcept{
 if(mapping_overlap(m,{&out,sizeof(out)}))return fail(Error::InvalidArgument);
 if(ids.size()!=24||id.kind!=constraint_graph_kind||!id.simulation||!id.generation)return fail(Error::InvalidArgument);
 if(overlap(&out,sizeof(out),colors.data(),colors.size_bytes())||overlap(&out,sizeof(out),ids.data(),ids.size_bytes())||overlap(&out,sizeof(out),scratch.identities.data(),scratch.identities.size_bytes())||overlap(&out,sizeof(out),scratch.color.data(),scratch.color.size_bytes()))return fail(Error::InvalidArgument);for(auto s:scratch.color_atoms)if(overlap(&out,sizeof(out),s.data(),s.size_bytes()))return fail(Error::InvalidArgument);for(const auto&c:colors)for(auto b:buffers(c))if(overlap(&out,sizeof(out),b.pointer,b.bytes))return fail(Error::InvalidArgument);
 for(auto r:{Range{scratch.color.data(),scratch.color.size_bytes()},Range{scratch.identities.data(),scratch.identities.size_bytes()},Range{m.roles.data(),m.roles.size_bytes()},Range{m.contact_endpoints.data(),m.contact_endpoints.size_bytes()}})if(overlap(&out,sizeof(out),r.pointer,r.bytes)||overlap(ids.data(),ids.size_bytes(),r.pointer,r.bytes))return fail(Error::InvalidArgument);for(auto a:scratch.color_atoms)if(overlap(ids.data(),ids.size_bytes(),a.data(),a.size_bytes()))return fail(Error::InvalidArgument);
 for(size_t i=0;i<ids.size();++i){if(ids[i].kind!=graph_color_kind||!ids[i].simulation||!ids[i].generation)return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(ids[i].simulation==ids[j].simulation)return fail(Error::InvalidArgument);}
 if(auto v=admission(colors,m,boundary,scratch);!v)return v;for(size_t i=0;i<24;++i)out.atoms[i]={0,ids[i]};out.record.identity=id;return {};
}
Status validate_restored_constraint_graph(const Record&root,std::span<const Record>records,std::span<const SpGraphColorView>colors,const GraphColorMaps&m,TreeCaptureBoundary boundary,ConstraintGraphScratch scratch)noexcept{
 if(root.identity.kind!=constraint_graph_kind||!root.identity.simulation||!root.identity.generation||root.fields.size()!=1||root.fields[0].id!=1||root.fields[0].atoms.size()!=24||records.size()!=24)return fail(Error::IncompatibleSchema);
 // Read-only canonical records must remain disjoint from scratch throughout recapture.
 for(auto r:{Range{&root,sizeof(root)},Range{root.fields.data(),root.fields.size_bytes()},Range{root.fields[0].atoms.data(),root.fields[0].atoms.size_bytes()},Range{records.data(),records.size_bytes()}}){if(overlap(r.pointer,r.bytes,scratch.color.data(),scratch.color.size_bytes())||overlap(r.pointer,r.bytes,scratch.identities.data(),scratch.identities.size_bytes()))return fail(Error::InvalidArgument);for(auto s:scratch.color_atoms)if(overlap(r.pointer,r.bytes,s.data(),s.size_bytes()))return fail(Error::InvalidArgument);}
 for(const auto&record:records){for(const auto&f:record.fields){for(auto r:{Range{record.fields.data(),record.fields.size_bytes()},Range{f.atoms.data(),f.atoms.size_bytes()}}){if(overlap(r.pointer,r.bytes,scratch.color.data(),scratch.color.size_bytes())||overlap(r.pointer,r.bytes,scratch.identities.data(),scratch.identities.size_bytes()))return fail(Error::InvalidArgument);for(auto s:scratch.color_atoms)if(overlap(r.pointer,r.bytes,s.data(),s.size_bytes()))return fail(Error::InvalidArgument);}}}
 if(auto v=admission(colors,m,boundary,scratch);!v)return v;GraphColorImage temporary(scratch.color_atoms);
 for(size_t i=0;i<24;++i){auto atom=root.fields[0].atoms[i];if(atom.bits||atom.identity.kind!=graph_color_kind||!atom.identity.simulation||!atom.identity.generation)return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(root.fields[0].atoms[j].identity.simulation==atom.identity.simulation)return fail(Error::InvalidArgument);const Record*child=nullptr;for(const auto&r:records)if(r.identity==atom.identity){if(child)return fail(Error::InvalidArgument);child=&r;}if(!child)return fail(Error::StaleGeneration);if(auto v=capture_graph_color(colors[i],uint32_t(i),atom.identity,m,boundary,scratch.color,temporary);!v)return v;if(!same(*child,temporary.record))return fail(Error::InvalidArgument);}
 return {};
}
}
