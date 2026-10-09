// SPDX-License-Identifier: MIT
#include "sensor_codec.hpp"
#include <algorithm>
#include <climits>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
constexpr std::array<FieldSpec,7> defs{{{1,AtomType::Reference,shape_kind,1,1,false},{2,AtomType::Unsigned,0,1,1,false},{3,AtomType::Unsigned,0,1,1,false},{4,AtomType::Unsigned,0,1,1,false},{5,AtomType::Reference,shape_kind,0,0,false},{6,AtomType::Reference,shape_kind,0,100000,false},{7,AtomType::Reference,shape_kind,0,100000,false}}};
struct Arrays{SpVisitor *p[3];uint32_t n[3],cap[3];};
Arrays arrays(const SpSensorView &v)noexcept{return {{v.hits,v.overlaps1,v.overlaps2},{v.hit_count,v.first_count,v.second_count},{v.hit_capacity,v.first_capacity,v.second_capacity}};}
struct Range{const void *p;size_t n;};
bool overlaps(Range a,Range b)noexcept{if(!a.n||!b.n)return false;auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return x<=y?y-x<a.n:x-y<b.n;}
bool disjoint(std::span<const Range> r)noexcept{for(size_t i=0;i<r.size();++i)for(size_t j=0;j<i;++j)if(overlaps(r[i],r[j]))return false;return true;}
bool canonical_less(Identity a,Identity b)noexcept{return a.simulation<b.simulation;}
Status validate_source(const SpSensorView &v,Identity id,const IdentityMap &active,const VersionedIdentityMap &history,std::span<Identity> scratch)noexcept{
 if(v.shape<0||id.kind!=sensor_kind||!id.simulation||!id.generation||v.hit_count)return fail(Error::InvalidArgument);
 auto shape=active.canonical(uint32_t(v.shape));if(!shape)return fail(shape.error());if(shape->kind!=shape_kind||shape->simulation!=id.simulation||shape->generation!=id.generation)return fail(Error::IncompatibleSchema);
 auto a=arrays(v);for(size_t c=0;c<3;++c){if(a.n[c]>a.cap[c]||a.cap[c]>100000||(!a.p[c]&&a.n[c])||scratch.size()<a.n[c])return fail(Error::CapacityExceeded);
  for(uint32_t i=0;i<a.n[c];++i){auto visitor=a.p[c][i];if(visitor.shape<0||!visitor.generation||(i&&a.p[c][i-1].shape>=visitor.shape))return fail(Error::InvalidArgument);auto ref=history.canonical({uint32_t(visitor.shape),visitor.generation});if(!ref)return fail(ref.error());if(ref->kind!=shape_kind)return fail(Error::IncompatibleSchema);scratch[i]=*ref;}
  std::sort(scratch.begin(),scratch.begin()+a.n[c],canonical_less);for(uint32_t i=1;i<a.n[c];++i)if(scratch[i-1].simulation==scratch[i].simulation)return fail(Error::InvalidArgument);
 }
 return {};
}
Result<int> validate_record(const Record &r,const IdentityMap &active,const VersionedIdentityMap &history,std::span<SpVisitor> scratch)noexcept{
 if(r.identity.kind!=sensor_kind||!r.identity.simulation||!r.identity.generation||r.fields.size()!=7)return fail(Error::IncompatibleSchema);
 for(size_t i=0;i<7;++i){const auto &f=r.fields[i];const auto &d=defs[i];if(f.id!=d.id||f.atoms.size()<d.minimum_atoms||f.atoms.size()>d.maximum_atoms)return fail(Error::IncompatibleSchema);
  for(const auto &a:f.atoms){if(d.type==AtomType::Reference){if(a.bits||a.identity.kind!=shape_kind||!a.identity.simulation||!a.identity.generation)return fail(Error::InvalidArgument);}else if(a.identity.kind||a.identity.simulation||a.identity.generation||a.bits>100000)return fail(Error::InvalidArgument);}}
 const auto identity=r.fields[0].atoms[0].identity;if(identity.simulation!=r.identity.simulation||identity.generation!=r.identity.generation)return fail(Error::IncompatibleSchema);auto own=active.native(identity);if(!own)return fail(own.error());if(*own>INT_MAX)return fail(Error::InvalidArgument);
 for(size_t c=0;c<3;++c){const auto refs=r.fields[c+4].atoms;if(refs.size()>r.fields[c+1].atoms[0].bits||scratch.size()<refs.size())return fail(Error::CapacityExceeded);
  for(size_t i=0;i<refs.size();++i){if(i&&refs[i-1].identity.simulation>=refs[i].identity.simulation)return fail(Error::NonCanonical);auto native=history.native(refs[i].identity);if(!native)return fail(native.error());if(native->slot>INT_MAX||!native->generation||native->generation>UINT16_MAX)return fail(Error::InvalidArgument);scratch[i]={int(native->slot),uint16_t(native->generation)};}
  std::sort(scratch.begin(),scratch.begin()+refs.size(),[](SpVisitor a,SpVisitor b){return a.shape<b.shape;});for(size_t i=1;i<refs.size();++i)if(scratch[i-1].shape==scratch[i].shape)return fail(Error::InvalidArgument);
 }
 return int(*own);
}
}
std::span<const FieldSpec> sensor_fields()noexcept{return defs;}
SensorImage::SensorImage(std::array<std::span<Atom>,3> s)noexcept:storage(s){for(size_t i=0;i<7;++i)fields[i].id=defs[i].id;for(size_t i=0;i<4;++i)fields[i].atoms=std::span(scalars).subspan(i,1);record.fields=fields;}
Status capture_sensor(const SpSensorView &v,Identity id,const IdentityMap &active,const VersionedIdentityMap &history,std::span<Identity> scratch,SensorImage &out)noexcept{
 const auto a=arrays(v);std::array<Range,9> ranges{{{&out,sizeof(out)},{&v,sizeof(v)},{scratch.data(),scratch.size_bytes()}}};for(size_t c=0;c<3;++c){ranges[c+3]={out.storage[c].data(),out.storage[c].size_bytes()};ranges[c+6]={a.p[c],size_t(a.n[c])*sizeof(SpVisitor)};if(out.storage[c].size()<a.n[c])return fail(Error::CapacityExceeded);}if(!disjoint(ranges))return fail(Error::InvalidArgument);
 if(auto s=validate_source(v,id,active,history,scratch);!s)return s;
 out.scalars[0]={0,*active.canonical(uint32_t(v.shape))};for(size_t c=0;c<3;++c){out.scalars[c+1]={a.cap[c],{}};for(uint32_t i=0;i<a.n[c];++i)scratch[i]=*history.canonical({uint32_t(a.p[c][i].shape),a.p[c][i].generation});std::sort(scratch.begin(),scratch.begin()+a.n[c],canonical_less);for(uint32_t i=0;i<a.n[c];++i)out.storage[c][i]={0,scratch[i]};out.fields[c+4].atoms=out.storage[c].first(a.n[c]);}out.record.identity=id;return {};
}
Status restore_sensor(const Record &r,const IdentityMap &active,const VersionedIdentityMap &history,std::span<SpVisitor> scratch,SpSensorView &out)noexcept{
 const auto a=arrays(out);std::array<Range,5> ranges{{{&out,sizeof(out)},{scratch.data(),scratch.size_bytes()}}};for(size_t c=0;c<3;++c)ranges[c+2]={a.p[c],size_t(a.cap[c])*sizeof(SpVisitor)};
 if(!disjoint(ranges))return fail(Error::InvalidArgument);for(auto w:ranges){if(overlaps(w,{&r,sizeof(r)})||overlaps(w,{r.fields.data(),r.fields.size_bytes()}))return fail(Error::InvalidArgument);for(auto f:r.fields)if(overlaps(w,{f.atoms.data(),f.atoms.size_bytes()}))return fail(Error::InvalidArgument);}
 auto own=validate_record(r,active,history,scratch);if(!own)return fail(own.error());for(size_t c=0;c<3;++c)if(a.cap[c]!=r.fields[c+1].atoms[0].bits||(!a.p[c]&&!r.fields[c+4].atoms.empty()))return fail(Error::CapacityExceeded);
 for(size_t c=0;c<3;++c){auto refs=r.fields[c+4].atoms;for(size_t i=0;i<refs.size();++i){auto native=*history.native(refs[i].identity);a.p[c][i]={int(native.slot),uint16_t(native.generation)};}if(refs.size()>1)std::sort(a.p[c],a.p[c]+refs.size(),[](SpVisitor a,SpVisitor b){return a.shape<b.shape;});}
 out.shape=*own;out.hit_count=0;out.first_count=uint32_t(r.fields[5].atoms.size());out.second_count=uint32_t(r.fields[6].atoms.size());return {};
}
}
