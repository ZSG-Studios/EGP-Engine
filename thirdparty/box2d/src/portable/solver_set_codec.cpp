// SPDX-License-Identifier: MIT
#include "solver_set_codec.hpp"
#include <algorithm>
#include <climits>
namespace superpos::box2d_portable { namespace {using namespace canonical;
constexpr size_t Joints=0;
constexpr size_t IslandsCap=1;
constexpr size_t Bodies=2;
constexpr size_t ContactsCap=3;
constexpr size_t Role=4;
constexpr size_t JointsCap=5;
constexpr size_t Contacts=6;
constexpr size_t StatesCap=7;
constexpr size_t Own=8;
constexpr size_t Islands=9;
constexpr size_t BodiesCap=10;
constexpr size_t States=11;
constexpr std::array<FieldSpec,12> defs{{
 {453519499u,AtomType::Reference,6400,0,100000,false}, // Joints
 {524389048u,AtomType::Unsigned,0,1,1,false}, // IslandsCap
 {1124724519u,AtomType::Reference,4100,0,100000,false}, // Bodies
 {1296706687u,AtomType::Unsigned,0,1,1,false}, // ContactsCap
 {2111786872u,AtomType::Unsigned,0,1,1,false}, // Role
 {2193052785u,AtomType::Unsigned,0,1,1,false}, // JointsCap
 {2255560442u,AtomType::Reference,4864,0,100000,false}, // Contacts
 {2385899193u,AtomType::Unsigned,0,1,1,false}, // StatesCap
 {3398001468u,AtomType::Reference,5121,1,1,false}, // Own
 {3546041940u,AtomType::Reference,5123,0,100000,false}, // Islands
 {3622963236u,AtomType::Unsigned,0,1,1,false}, // BodiesCap
 {3766062639u,AtomType::Reference,4098,0,100000,false}, // States
}};
constexpr size_t columns[5]={Bodies,States,Joints,Contacts,Islands};
constexpr size_t capacities[5]={BodiesCap,StatesCap,JointsCap,ContactsCap,IslandsCap};
constexpr uint32_t kinds[5]={bodysim_kind,bodystate_kind,joint_sim_kind,contact_sim_kind,island_sim_kind};
struct Layout {int *p[5];uint32_t count[5],capacity[5];};
Layout layout(const SpSolverMembership &v)noexcept{return {{v.body_ids,v.body_ids,v.joint_ids,v.contact_ids,v.island_ids},{v.body_count,v.state_count,v.joint_count,v.contact_count,v.island_count},{v.body_capacity,v.state_capacity,v.joint_capacity,v.contact_capacity,v.island_capacity}};}
const IdentityMap &map_at(const SolverMembershipMaps &m,size_t i)noexcept{return i<2?m.body:i==2?m.joint:i==3?m.contact:m.island;}
uint32_t native_kind(size_t i)noexcept{return i<2?body_kind:i==2?joint_kind:i==3?contact_kind:island_kind;}
struct Range {const void *p;size_t n;};
bool overlaps(Range a,Range b)noexcept{if(!a.n||!b.n)return false;auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return x<=y?y-x<a.n:x-y<b.n;}
bool separate(std::span<const Range> a)noexcept{for(size_t i=0;i<a.size();++i)for(size_t j=0;j<i;++j)if(overlaps(a[i],a[j]))return false;return true;}
Status unique(std::span<Identity> ids)noexcept{std::sort(ids.begin(),ids.end(),[](Identity a,Identity b){return a.simulation<b.simulation||(a.simulation==b.simulation&&a.generation<b.generation);});for(size_t i=1;i<ids.size();++i)if(ids[i]==ids[i-1])return fail(Error::InvalidArgument);return {};}
Result<Identity> canonical_member(const SolverMembershipMaps &m,size_t column,int n)noexcept{if(n<0)return fail(Error::InvalidArgument);auto id=map_at(m,column).canonical(uint32_t(n));if(!id)return fail(id.error());if(id->kind!=native_kind(column))return fail(Error::IncompatibleSchema);id->kind=kinds[column];return *id;}
Result<int> native_member(const SolverMembershipMaps &m,size_t column,Identity id)noexcept{if(id.kind!=kinds[column])return fail(Error::IncompatibleSchema);id.kind=native_kind(column);auto n=map_at(m,column).native(id);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);return int(*n);}
uint32_t role_of(int slot)noexcept{return slot<3?uint32_t(slot):3;}
Status shape(uint32_t role,const Layout &v)noexcept{
 if(role>3)return fail(Error::InvalidArgument);
 for(size_t i=0;i<5;++i)if(v.count[i]>v.capacity[i]||v.capacity[i]>100000||(!v.p[i]&&v.count[i]))return fail(Error::CapacityExceeded);
 if(role==2){if(v.count[1]!=v.count[0]||v.count[2])return fail(Error::InvalidArgument);}
 else if(v.count[1])return fail(Error::InvalidArgument);
 if(role<2&&v.count[4])return fail(Error::InvalidArgument);
 if(role==0&&v.count[3])return fail(Error::InvalidArgument);
 return {};
}
Status native_valid(const SpSolverMembership &v,Identity id,const SolverMembershipMaps &m,std::span<Identity> scratch)noexcept{
 if(v.set_index<0||id.kind!=solver_set_kind||!id.simulation||!id.generation)return fail(Error::InvalidArgument);
 auto own=m.set.canonical(uint32_t(v.set_index));if(!own)return fail(own.error());if(*own!=id)return fail(Error::StaleGeneration);
 const auto l=layout(v);if(auto s=shape(role_of(v.set_index),l);!s)return s;
 for(size_t c=0;c<5;++c){if(scratch.size()<l.count[c])return fail(Error::CapacityExceeded);for(uint32_t i=0;i<l.count[c];++i){auto id=canonical_member(m,c,l.p[c][i]);if(!id)return fail(id.error());scratch[i]=*id;}if(auto s=unique(scratch.first(l.count[c]));!s)return s;}
 return {};
}
Status record_valid(const Record &r,const SolverMembershipMaps &m,std::span<Identity> scratch)noexcept{
 if(r.identity.kind!=solver_set_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=defs.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<defs.size();++i){auto &f=r.fields[i];auto &d=defs[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);for(const auto &a:f.atoms){if(d.type==AtomType::Reference){if(a.bits||a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);}else if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>100000)return fail(Error::InvalidArgument);}}
 if(r.fields[Own].atoms[0].identity!=r.identity)return fail(Error::StaleGeneration);
 auto own=m.set.native(r.identity);if(!own||*own>INT_MAX)return fail(Error::StaleGeneration);uint32_t role=uint32_t(r.fields[Role].atoms[0].bits);if(role!=role_of(int(*own)))return fail(Error::IncompatibleSchema);
 int present=0;Layout l{};for(size_t c=0;c<5;++c){l.count[c]=uint32_t(r.fields[columns[c]].atoms.size());l.capacity[c]=uint32_t(r.fields[capacities[c]].atoms[0].bits);l.p[c]=&present;}if(auto s=shape(role,l);!s)return s;
 if(role==2)for(size_t i=0;i<l.count[0];++i){auto b=r.fields[Bodies].atoms[i].identity,s=r.fields[States].atoms[i].identity;if(b.simulation!=s.simulation||b.generation!=s.generation)return fail(Error::IncompatibleSchema);}
 for(size_t c=0;c<5;++c){if(scratch.size()<l.count[c])return fail(Error::CapacityExceeded);for(uint32_t i=0;i<l.count[c];++i){auto a=r.fields[columns[c]].atoms[i];auto n=native_member(m,c,a.identity);if(!n)return fail(n.error());scratch[i]=a.identity;}if(auto s=unique(scratch.first(l.count[c]));!s)return s;}
 return {};
}
}
std::span<const FieldSpec> solver_membership_fields()noexcept{return defs;}
SolverMembershipImage::SolverMembershipImage(std::array<std::span<Atom>,5> arrays)noexcept:storage(arrays){for(size_t i=0;i<12;++i)fields[i].id=defs[i].id;fields[Own].atoms=std::span(scalars).subspan(0,1);fields[Role].atoms=std::span(scalars).subspan(1,1);for(size_t i=0;i<5;++i)fields[capacities[i]].atoms=std::span(scalars).subspan(i+2,1);record.fields=fields;}
Status capture_solver_membership(const SpSolverMembership &v,Identity id,const SolverMembershipMaps &m,std::span<Identity> scratch,SolverMembershipImage &out)noexcept{
 const auto l=layout(v);std::array<Range,12> ranges{};ranges[0]={&out,sizeof(out)};ranges[1]={scratch.data(),scratch.size_bytes()};ranges[2]={&v,sizeof(v)};
 for(size_t c=0;c<5;++c){ranges[c+3]={out.storage[c].data(),out.storage[c].size_bytes()};if(out.storage[c].size()<l.count[c])return fail(Error::CapacityExceeded);}
 const size_t ncols[]={0,2,3,4};for(size_t i=0;i<4;++i){size_t c=ncols[i];ranges[i+8]={l.p[c],size_t(l.count[c])*sizeof(int)};}if(!separate(ranges))return fail(Error::InvalidArgument);
 if(auto s=native_valid(v,id,m,scratch);!s)return s;
 out.scalars[0]={0,id};out.scalars[1]={role_of(v.set_index),{}};
 for(size_t c=0;c<5;++c){out.scalars[c+2]={l.capacity[c],{}};out.fields[columns[c]].atoms=out.storage[c].first(l.count[c]);for(uint32_t i=0;i<l.count[c];++i)out.storage[c][i]={0,*canonical_member(m,c,l.p[c][i])};}
 out.record.identity=id;return {};
}
Status restore_solver_membership(const Record &r,const SolverMembershipMaps &m,std::span<Identity> scratch,SpSolverMembership &out)noexcept{
 const auto l=layout(out);const Range writes[]={{&out,sizeof(out)},{scratch.data(),scratch.size_bytes()},{out.body_ids,size_t(out.body_capacity)*sizeof(int)},{out.joint_ids,size_t(out.joint_capacity)*sizeof(int)},{out.contact_ids,size_t(out.contact_capacity)*sizeof(int)},{out.island_ids,size_t(out.island_capacity)*sizeof(int)}};
 if(!separate(writes))return fail(Error::InvalidArgument);for(auto w:writes){if(overlaps(w,{&r,sizeof(r)})||overlaps(w,{r.fields.data(),r.fields.size_bytes()}))return fail(Error::InvalidArgument);for(auto f:r.fields)if(overlaps(w,{f.atoms.data(),f.atoms.size_bytes()}))return fail(Error::InvalidArgument);}
 if(auto s=record_valid(r,m,scratch);!s)return s;
 for(size_t c=0;c<5;++c)if(l.capacity[c]!=r.fields[capacities[c]].atoms[0].bits||(!l.p[c]&&!r.fields[columns[c]].atoms.empty()))return fail(Error::CapacityExceeded);
 for(size_t c=0;c<5;++c)if(c!=1)for(size_t i=0;i<r.fields[columns[c]].atoms.size();++i)l.p[c][i]=*native_member(m,c,r.fields[columns[c]].atoms[i].identity);
 out.set_index=int(*m.set.native(r.identity));out.body_count=uint32_t(r.fields[Bodies].atoms.size());out.state_count=uint32_t(r.fields[States].atoms.size());out.joint_count=uint32_t(r.fields[Joints].atoms.size());out.contact_count=uint32_t(r.fields[Contacts].atoms.size());out.island_count=uint32_t(r.fields[Islands].atoms.size());return {};
}
}

namespace superpos::box2d_portable {
namespace {constexpr std::array<canonical::FieldSpec,1> island_sim_schema{{{1,canonical::AtomType::Reference,island_kind,1,1,false}}};}
IslandSimImage::IslandSimImage()noexcept{field={1,std::span(&atom,1)};record.fields=std::span(&field,1);}
std::span<const canonical::FieldSpec> island_sim_fields()noexcept{return island_sim_schema;}
Status capture_island_sim(int id,canonical::Identity identity,const canonical::IdentityMap &map,IslandSimImage &out)noexcept{
 if(id<0||identity.kind!=island_sim_kind||!identity.simulation||!identity.generation)return fail(Error::InvalidArgument);auto token=map.canonical(uint32_t(id));if(!token)return fail(token.error());if(token->kind!=island_kind||token->simulation!=identity.simulation||token->generation!=identity.generation)return fail(Error::StaleGeneration);out.atom={0,*token};out.record.identity=identity;return {};
}
Result<int> restore_island_sim(const canonical::Record &r,const canonical::IdentityMap &map)noexcept{
 if(r.identity.kind!=island_sim_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=1||r.fields[0].id!=1||r.fields[0].atoms.size()!=1)return fail(Error::IncompatibleSchema);const auto &a=r.fields[0].atoms[0];if(a.bits||a.identity.kind!=island_kind||a.identity.simulation!=r.identity.simulation||a.identity.generation!=r.identity.generation)return fail(Error::InvalidArgument);auto n=map.native(a.identity);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);return int(*n);
}
}
