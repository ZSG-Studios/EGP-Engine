// SPDX-License-Identifier: MIT
#include "chain_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <climits>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,11> defs{{{1,AtomType::Reference,chain_kind,1,1,false},{2,AtomType::Reference,body_kind,1,1,false},{3,AtomType::Reference,chain_kind,1,1,true},{4,AtomType::Unsigned,0,1,1,false},{5,AtomType::Reference,shape_kind,1,100000,false},{6,AtomType::Float32,0,1,100000,false},{7,AtomType::Float32,0,1,100000,false},{8,AtomType::Float32,0,1,100000,false},{9,AtomType::Float32,0,1,100000,false},{10,AtomType::Unsigned,0,1,100000,false},{11,AtomType::Unsigned,0,1,100000,false}}};
struct Range{const void *p;size_t n;};
bool overlap(Range a,Range b)noexcept{if(!a.n||!b.n)return false;auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return x<=y?y-x<a.n:x-y<b.n;}
bool disjoint(std::span<const Range> a)noexcept{for(size_t i=0;i<a.size();++i)for(size_t j=0;j<i;++j)if(overlap(a[i],a[j]))return false;return true;}
Status unique(std::span<Identity> ids)noexcept{std::sort(ids.begin(),ids.end(),[](Identity a,Identity b){return a.simulation<b.simulation;});for(size_t i=1;i<ids.size();++i)if(ids[i].simulation==ids[i-1].simulation)return fail(Error::InvalidArgument);return {};}
Result<int> slot(const IdentityMap &m,Identity id)noexcept{auto n=m.native(id);if(!n)return fail(n.error());if(*n>INT_MAX)return fail(Error::InvalidArgument);return int(*n);}
bool valid_material(const b2SurfaceMaterial &m)noexcept{return std::isfinite(m.friction)&&m.friction>=0&&std::isfinite(m.restitution)&&m.restitution>=0&&std::isfinite(m.rollingResistance)&&m.rollingResistance>=0&&std::isfinite(m.tangentSpeed);}
Status valid_source(const SpChainView &v,Identity id,const ChainMaps &maps,std::span<Identity> scratch)noexcept{
 if(v.id<0||v.body<0||v.next< -1||!v.generation||id.kind!=chain_kind||!id.simulation||!id.generation||!v.count||!v.material_count||v.count>100000||v.material_count>100000||v.count>v.shape_capacity||v.material_count>v.material_capacity||!v.shapes||!v.materials||v.next==v.id)return fail(Error::InvalidArgument);
 if(scratch.size()<v.count)return fail(Error::CapacityExceeded);auto own=maps.chain.canonical(uint32_t(v.id)),body=maps.body.canonical(uint32_t(v.body));if(!own||!body||*own!=id||body->kind!=body_kind)return fail(Error::StaleGeneration);
 if(v.next!=-1){auto next=maps.chain.canonical(uint32_t(v.next));if(!next||next->kind!=chain_kind)return fail(Error::StaleGeneration);}
 for(uint32_t i=0;i<v.count;++i){if(v.shapes[i]<0)return fail(Error::InvalidArgument);auto shape=maps.shape.canonical(uint32_t(v.shapes[i]));if(!shape||shape->kind!=shape_kind)return fail(Error::StaleGeneration);scratch[i]=*shape;}if(auto s=unique(scratch.first(v.count));!s)return s;
 for(uint32_t i=0;i<v.material_count;++i)if(!valid_material(v.materials[i]))return fail(Error::InvalidArgument);return {};
}
Status valid_record(const Record &r,const ChainMaps &maps,std::span<Identity> scratch)noexcept{
 if(r.identity.kind!=chain_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=11)return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<11;++i){auto &f=r.fields[i];auto &d=defs[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);for(const auto &a:f.atoms){if(d.type==AtomType::Reference){bool nil=!a.identity.kind&&!a.identity.simulation&&!a.identity.generation;if(a.bits||(nil?!d.nullable_reference:(a.identity.kind!=d.reference_kind||!a.identity.simulation||!a.identity.generation)))return fail(Error::InvalidArgument);}else if(a.identity.kind||a.identity.simulation||a.identity.generation)return fail(Error::InvalidArgument);else if(d.type==AtomType::Float32&&(a.bits>UINT32_MAX||!std::isfinite(std::bit_cast<float>(uint32_t(a.bits)))||(i<8&&std::bit_cast<float>(uint32_t(a.bits))<0)))return fail(Error::NonCanonical);}}
 if(r.fields[0].atoms[0].identity!=r.identity||!slot(maps.chain,r.identity)||!slot(maps.body,r.fields[1].atoms[0].identity))return fail(Error::StaleGeneration);
 const auto next=r.fields[2].atoms[0].identity;if(next.kind&&(next==r.identity||!slot(maps.chain,next)))return fail(Error::StaleGeneration);
 if(!r.fields[3].atoms[0].bits||r.fields[3].atoms[0].bits>UINT16_MAX)return fail(Error::InvalidArgument);auto count=r.fields[4].atoms.size(),materials=r.fields[5].atoms.size();if(scratch.size()<count)return fail(Error::CapacityExceeded);
 for(size_t c=6;c<11;++c)if(r.fields[c].atoms.size()!=materials)return fail(Error::InvalidArgument);
 for(size_t i=0;i<count;++i){if(!slot(maps.shape,r.fields[4].atoms[i].identity))return fail(Error::StaleGeneration);scratch[i]=r.fields[4].atoms[i].identity;}if(auto s=unique(scratch.first(count));!s)return s;
 for(const auto &color:r.fields[10].atoms)if(color.bits>UINT32_MAX)return fail(Error::InvalidArgument);return {};
}
}
std::span<const FieldSpec> chain_fields()noexcept{return defs;}
ChainImage::ChainImage(std::array<std::span<Atom>,7> spans)noexcept:storage(spans){for(size_t i=0;i<11;++i)fields[i].id=defs[i].id;for(size_t i=0;i<4;++i)fields[i].atoms=std::span(scalars).subspan(i,1);record.fields=fields;}
Status capture_chain(const SpChainView &v,Identity id,const ChainMaps &maps,std::span<Identity> scratch,ChainImage &out)noexcept{
 std::array<Range,12> ranges{};ranges[0]={&out,sizeof(out)};ranges[1]={&v,sizeof(v)};ranges[2]={scratch.data(),scratch.size_bytes()};ranges[3]={v.shapes,size_t(v.count)*sizeof(int)};ranges[4]={v.materials,size_t(v.material_count)*sizeof(b2SurfaceMaterial)};
 for(size_t i=0;i<7;++i){ranges[i+5]={out.storage[i].data(),out.storage[i].size_bytes()};if(out.storage[i].size()<(i==0?v.count:v.material_count))return fail(Error::CapacityExceeded);}if(!disjoint(ranges))return fail(Error::InvalidArgument);
 if(auto s=valid_source(v,id,maps,scratch);!s)return s;out.scalars[0]={0,id};out.scalars[1]={0,*maps.body.canonical(uint32_t(v.body))};out.scalars[2]={};if(v.next!=-1)out.scalars[2].identity=*maps.chain.canonical(uint32_t(v.next));out.scalars[3]={v.generation,{}};
 for(size_t c=0;c<7;++c)out.fields[c+4].atoms=out.storage[c].first(c==0?v.count:v.material_count);
 for(uint32_t i=0;i<v.count;++i)out.storage[0][i]={0,*maps.shape.canonical(uint32_t(v.shapes[i]))};
 for(uint32_t i=0;i<v.material_count;++i){auto &m=v.materials[i];out.storage[1][i]={std::bit_cast<uint32_t>(m.friction),{}};out.storage[2][i]={std::bit_cast<uint32_t>(m.restitution),{}};out.storage[3][i]={std::bit_cast<uint32_t>(m.rollingResistance),{}};out.storage[4][i]={std::bit_cast<uint32_t>(m.tangentSpeed),{}};out.storage[5][i]={m.userMaterialId,{}};out.storage[6][i]={m.customColor,{}};}out.record.identity=id;return {};
}
Status restore_chain(const Record &r,const ChainMaps &maps,std::span<Identity> scratch,SpChainView &out)noexcept{
 const Range writes[]={{&out,sizeof(out)},{scratch.data(),scratch.size_bytes()},{out.shapes,size_t(out.shape_capacity)*sizeof(int)},{out.materials,size_t(out.material_capacity)*sizeof(b2SurfaceMaterial)}};if(!disjoint(writes))return fail(Error::InvalidArgument);for(auto w:writes){if(overlap(w,{&r,sizeof(r)})||overlap(w,{r.fields.data(),r.fields.size_bytes()}))return fail(Error::InvalidArgument);for(auto f:r.fields)if(overlap(w,{f.atoms.data(),f.atoms.size_bytes()}))return fail(Error::InvalidArgument);}
 if(auto s=valid_record(r,maps,scratch);!s)return s;const auto count=r.fields[4].atoms.size(),materials=r.fields[5].atoms.size();if(out.shape_capacity!=count||out.material_capacity!=materials||!out.shapes||!out.materials)return fail(Error::CapacityExceeded);
 for(size_t i=0;i<count;++i)out.shapes[i]=*slot(maps.shape,r.fields[4].atoms[i].identity);
 for(size_t i=0;i<materials;++i){b2SurfaceMaterial m{};m.friction=std::bit_cast<float>(uint32_t(r.fields[5].atoms[i].bits));m.restitution=std::bit_cast<float>(uint32_t(r.fields[6].atoms[i].bits));m.rollingResistance=std::bit_cast<float>(uint32_t(r.fields[7].atoms[i].bits));m.tangentSpeed=std::bit_cast<float>(uint32_t(r.fields[8].atoms[i].bits));m.userMaterialId=r.fields[9].atoms[i].bits;m.customColor=uint32_t(r.fields[10].atoms[i].bits);out.materials[i]=m;}
 out.id=*slot(maps.chain,r.identity);out.body=*slot(maps.body,r.fields[1].atoms[0].identity);const auto next=r.fields[2].atoms[0].identity;out.next=next.kind?*slot(maps.chain,next):-1;out.generation=uint16_t(r.fields[3].atoms[0].bits);out.count=uint32_t(count);out.material_count=uint32_t(materials);return {};
}
}
