// SPDX-License-Identifier: MIT
#include "island_codec.hpp"
#include <algorithm>
#include <climits>
namespace superpos::box2d_portable {
namespace {using namespace canonical;
constexpr size_t JointA=0;
constexpr size_t Contacts=1;
constexpr size_t Bodies=2;
constexpr size_t Member=3;
constexpr size_t ContactA=4;
constexpr size_t Id=5;
constexpr size_t ContactB=6;
constexpr size_t ContactCap=7;
constexpr size_t Joints=8;
constexpr size_t Set=9;
constexpr size_t JointCap=10;
constexpr size_t Removed=11;
constexpr size_t BodyCap=12;
constexpr size_t JointB=13;
constexpr std::array<FieldSpec,14> defs{{
 {391745954u,AtomType::Reference,4097,0,100000,false}, // JointA
 {444770251u,AtomType::Reference,4102,0,100000,false}, // Contacts
 {579523239u,AtomType::Reference,4097,0,100000,false}, // Bodies
 {911775579u,AtomType::Reference,5122,1,1,false}, // Member
 {1035134141u,AtomType::Reference,4097,0,100000,false}, // ContactA
 {1273574113u,AtomType::Reference,5122,1,1,false}, // Id
 {1415831562u,AtomType::Reference,4097,0,100000,false}, // ContactB
 {1441770096u,AtomType::Unsigned,0,1,1,false}, // ContactCap
 {1444095443u,AtomType::Reference,4104,0,100000,false}, // Joints
 {1757874631u,AtomType::Reference,5121,1,1,false}, // Set
 {2603173449u,AtomType::Unsigned,0,1,1,false}, // JointCap
 {2838363454u,AtomType::Unsigned,0,1,1,false}, // Removed
 {3443623922u,AtomType::Unsigned,0,1,1,false}, // BodyCap
 {3850047040u,AtomType::Reference,4097,0,100000,false}, // JointB
}};
constexpr uint32_t maximum=100000;
struct Range{const void *data;size_t size;};
bool overlap(Range a,Range b)noexcept{if(!a.size||!b.size)return false;auto x=reinterpret_cast<uintptr_t>(a.data),y=reinterpret_cast<uintptr_t>(b.data);return x<=y?y-x<a.size:x-y<b.size;}
bool disjoint(std::span<const Range> ranges)noexcept{for(size_t i=0;i<ranges.size();++i)for(size_t j=0;j<i;++j)if(overlap(ranges[i],ranges[j]))return false;return true;}
bool less_id(Identity a,Identity b)noexcept{return a.simulation<b.simulation||(a.simulation==b.simulation&&a.generation<b.generation);}
Status unique(std::span<Identity> scratch)noexcept{std::sort(scratch.begin(),scratch.end(),less_id);for(size_t i=1;i<scratch.size();++i)if(scratch[i]==scratch[i-1])return fail(Error::InvalidArgument);return {};}
Result<Identity> token(const IdentityMap &m,int n)noexcept{if(n<0)return fail(Error::InvalidArgument);return m.canonical(uint32_t(n));}
Result<int> slot(const IdentityMap &m,const Atom &a)noexcept{if(a.bits)return fail(Error::InvalidArgument);auto n=m.native(a.identity);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);return int(*n);}
Status native_valid(const SpIslandView &v,Identity id,const IslandMappings &m,std::span<Identity> scratch)noexcept{
 if(id.kind!=island_kind||!id.simulation||!id.generation||v.removed_constraints<0||v.body_count>v.body_capacity||v.contact_count>v.contact_capacity||v.joint_count>v.joint_capacity||v.body_capacity>maximum||v.contact_capacity>maximum||v.joint_capacity>maximum||(!v.bodies&&v.body_count)||(!v.contacts&&v.contact_count)||(!v.joints&&v.joint_count))return fail(Error::InvalidArgument);
 if(scratch.size()<std::max({v.body_count,v.contact_count,v.joint_count}))return fail(Error::CapacityExceeded);
 auto own=token(m.island,v.island_id),set=token(m.solver_set,v.set_index),member=token(m.set_member,v.local_index);
 if(!own||!set||!member)return fail(Error::StaleGeneration);if(*own!=id||*member!=id)return fail(Error::StaleGeneration);
 for(uint32_t i=0;i<v.body_count;++i){auto t=token(m.body,v.bodies[i]);if(!t)return fail(t.error());scratch[i]=*t;}if(auto x=unique(scratch.first(v.body_count));!x)return x;
 for(int k=0;k<2;++k){auto n=k?v.joint_count:v.contact_count;auto links=k?v.joints:v.contacts;const auto &map=k?m.joint:m.contact;
  for(uint32_t i=0;i<n;++i){auto t=token(map,links[i].object),a=token(m.body,links[i].body_a),b=token(m.body,links[i].body_b);if(!t||!a||!b)return fail(Error::StaleGeneration);if(*a==*b)return fail(Error::InvalidArgument);scratch[i]=*t;}
  if(auto x=unique(scratch.first(n));!x)return x;}
 return {};
}
Status record_valid(const Record &r,const IslandMappings &m,std::span<Identity> scratch)noexcept{
 if(r.identity.kind!=island_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=defs.size())return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<defs.size();++i){const auto &f=r.fields[i];const auto &d=defs[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);
  for(const auto &a:f.atoms){if(d.type==AtomType::Reference){if(a.bits||a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);}else if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>(i==Removed?uint64_t(INT_MAX):maximum))return fail(Error::InvalidArgument);}}
 if(r.fields[Removed].atoms[0].bits>INT_MAX||r.fields[Id].atoms[0].identity!=r.identity||r.fields[Member].atoms[0].identity!=r.identity)return fail(Error::InvalidArgument);
 if(!slot(m.island,r.fields[Id].atoms[0])||!slot(m.set_member,r.fields[Member].atoms[0])||!slot(m.solver_set,r.fields[Set].atoms[0]))return fail(Error::StaleGeneration);
 const auto bc=r.fields[Bodies].atoms.size(),cc=r.fields[Contacts].atoms.size(),jc=r.fields[Joints].atoms.size();
 if(bc>r.fields[BodyCap].atoms[0].bits||cc>r.fields[ContactCap].atoms[0].bits||jc>r.fields[JointCap].atoms[0].bits||r.fields[ContactA].atoms.size()!=cc||r.fields[ContactB].atoms.size()!=cc||r.fields[JointA].atoms.size()!=jc||r.fields[JointB].atoms.size()!=jc)return fail(Error::InvalidArgument);
 if(scratch.size()<std::max({bc,cc,jc}))return fail(Error::CapacityExceeded);
 for(size_t i=0;i<bc;++i){auto v=slot(m.body,r.fields[Bodies].atoms[i]);if(!v)return fail(v.error());scratch[i]=r.fields[Bodies].atoms[i].identity;}if(auto x=unique(scratch.first(bc));!x)return x;
 for(int k=0;k<2;++k){auto oi=k?Joints:Contacts,ai=k?JointA:ContactA,bi=k?JointB:ContactB;auto n=r.fields[oi].atoms.size();const auto &map=k?m.joint:m.contact;
  for(size_t i=0;i<n;++i){auto o=slot(map,r.fields[oi].atoms[i]),a=slot(m.body,r.fields[ai].atoms[i]),b=slot(m.body,r.fields[bi].atoms[i]);if(!o||!a||!b)return fail(Error::StaleGeneration);if(*a==*b)return fail(Error::InvalidArgument);scratch[i]=r.fields[oi].atoms[i].identity;}if(auto x=unique(scratch.first(n));!x)return x;}
 return {};
}
}
std::span<const FieldSpec> island_fields()noexcept{return defs;}
IslandImage::IslandImage(std::span<Atom> b,std::span<Atom> c,std::span<Atom> j)noexcept:body_storage(b),contact_storage(c),joint_storage(j){for(size_t i=0;i<defs.size();++i)fields[i].id=defs[i].id;
 const size_t indices[]={Set,Member,Id,Removed,BodyCap,ContactCap,JointCap};for(size_t i=0;i<7;++i)fields[indices[i]].atoms=std::span(scalars).subspan(i,1);record.fields=fields;}
Status capture_island(const SpIslandView &v,Identity id,const IslandMappings &m,std::span<Identity> scratch,IslandImage &out)noexcept{
 const Range ranges[]={{&out,sizeof(out)},{out.body_storage.data(),out.body_storage.size_bytes()},{out.contact_storage.data(),out.contact_storage.size_bytes()},{out.joint_storage.data(),out.joint_storage.size_bytes()},{scratch.data(),scratch.size_bytes()},{&v,sizeof(v)},{v.bodies,size_t(v.body_count)*sizeof(int)},{v.contacts,size_t(v.contact_count)*sizeof(SpIslandLink)},{v.joints,size_t(v.joint_count)*sizeof(SpIslandLink)}};
 if(!disjoint(ranges))return fail(Error::InvalidArgument);
 if(out.body_storage.size()<v.body_count||out.contact_storage.size()<size_t(v.contact_count)*3||out.joint_storage.size()<size_t(v.joint_count)*3)return fail(Error::CapacityExceeded);
 if(auto s=native_valid(v,id,m,scratch);!s)return s;
 out.scalars[0]={0,*token(m.solver_set,v.set_index)};out.scalars[1]={0,*token(m.set_member,v.local_index)};out.scalars[2]={0,id};out.scalars[3]={uint64_t(v.removed_constraints),{}};out.scalars[4]={v.body_capacity,{}};out.scalars[5]={v.contact_capacity,{}};out.scalars[6]={v.joint_capacity,{}};
 out.fields[Bodies].atoms=out.body_storage.first(v.body_count);for(uint32_t i=0;i<v.body_count;++i)out.body_storage[i]={0,*token(m.body,v.bodies[i])};
 for(int k=0;k<2;++k){auto n=k?v.joint_count:v.contact_count;auto links=k?v.joints:v.contacts;auto storage=k?out.joint_storage:out.contact_storage;const auto &map=k?m.joint:m.contact;const size_t indices[]={k?Joints:Contacts,k?JointA:ContactA,k?JointB:ContactB};for(size_t z=0;z<3;++z)out.fields[indices[z]].atoms=storage.subspan(z*n,n);
  for(uint32_t i=0;i<n;++i){storage[i]={0,*token(map,links[i].object)};storage[n+i]={0,*token(m.body,links[i].body_a)};storage[2*n+i]={0,*token(m.body,links[i].body_b)};}}
 out.record.identity=id;return {};
}
Status restore_island(const Record &r,const IslandMappings &m,std::span<Identity> scratch,SpIslandView &out)noexcept{
 // Reject aliases before scratch sorting or staging writes, including every
 // incoming atom column. Binding maps must remain frozen for this operation.
 const Range writes[]={{&out,sizeof(out)},{out.bodies,size_t(out.body_capacity)*sizeof(int)},{out.contacts,size_t(out.contact_capacity)*sizeof(SpIslandLink)},{out.joints,size_t(out.joint_capacity)*sizeof(SpIslandLink)},{scratch.data(),scratch.size_bytes()}};
 if(!disjoint(writes))return fail(Error::InvalidArgument);
 for(auto w:writes){if(overlap(w,{&r,sizeof(r)})||overlap(w,{r.fields.data(),r.fields.size_bytes()}))return fail(Error::InvalidArgument);for(auto f:r.fields)if(overlap(w,{f.atoms.data(),f.atoms.size_bytes()}))return fail(Error::InvalidArgument);}
 if(auto s=record_valid(r,m,scratch);!s)return s;
 auto bc=r.fields[Bodies].atoms.size(),cc=r.fields[Contacts].atoms.size(),jc=r.fields[Joints].atoms.size();
 if(out.body_capacity!=r.fields[BodyCap].atoms[0].bits||out.contact_capacity!=r.fields[ContactCap].atoms[0].bits||out.joint_capacity!=r.fields[JointCap].atoms[0].bits||(!out.bodies&&bc)||(!out.contacts&&cc)||(!out.joints&&jc))return fail(Error::CapacityExceeded);
 for(size_t i=0;i<bc;++i)out.bodies[i]=*slot(m.body,r.fields[Bodies].atoms[i]);
 for(int k=0;k<2;++k){auto oi=k?Joints:Contacts,ai=k?JointA:ContactA,bi=k?JointB:ContactB;auto links=k?out.joints:out.contacts;const auto &map=k?m.joint:m.contact;for(size_t i=0;i<r.fields[oi].atoms.size();++i)links[i]={*slot(map,r.fields[oi].atoms[i]),*slot(m.body,r.fields[ai].atoms[i]),*slot(m.body,r.fields[bi].atoms[i])};}
 out.body_count=uint32_t(bc);out.contact_count=uint32_t(cc);out.joint_count=uint32_t(jc);out.island_id=*slot(m.island,r.fields[Id].atoms[0]);out.set_index=*slot(m.solver_set,r.fields[Set].atoms[0]);out.local_index=*slot(m.set_member,r.fields[Member].atoms[0]);out.removed_constraints=int(r.fields[Removed].atoms[0].bits);return {};
}
}
