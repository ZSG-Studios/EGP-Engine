// SPDX-License-Identifier: MIT
#include "prepared_maps.hpp"
namespace superpos::box2d_portable {using namespace canonical;
namespace {struct Range{const void*p;size_t n;};bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.n&&b.n&&(x<=y?y-x<a.n:x-y<b.n);}}
Status derive_prepared_maps(const WorldOwnerMap&body,const WorldOwnerMap&constraints,std::span<const uint32_t>slots,std::span<NativeBinding>bindings,std::span<uint32_t>order,std::span<std::optional<PreparedMap>>output)noexcept{
 if(!body.lease_active()||!constraints.lease_active())return fail(Error::NotReady);
 if(body.kind()!=SP_OWNER_BODY||(constraints.kind()!=SP_OWNER_CONTACT&&constraints.kind()!=SP_OWNER_JOINT)||&body.source()!=&constraints.source())return fail(Error::InvalidArgument);
 if(slots.size()>100000||bindings.size()<2*slots.size()||order.size()<2*slots.size()||output.size()<slots.size())return fail(Error::CapacityExceeded);
 const Range writes[]={{bindings.data(),bindings.size_bytes()},{order.data(),order.size_bytes()},{output.data(),output.size_bytes()}};
 const Range reads[]={{slots.data(),slots.size_bytes()},{&body,sizeof(body)},{&constraints,sizeof(constraints)}};
 for(size_t i=0;i<3;++i){auto r=writes[i];if(body.overlaps_storage(r.p,r.n)||constraints.overlaps_storage(r.p,r.n)||spWorldOwnershipOverlaps(&body.source(),r.p,r.n))return fail(Error::InvalidArgument);for(auto in:reads)if(overlap(r,in))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(r,writes[j]))return fail(Error::InvalidArgument);}
 for(size_t i=0;i<slots.size();++i)if((i&&slots[i-1]>=slots[i])||slots[i]>INT32_MAX)return fail(Error::InvalidArgument);
 for(size_t i=0;i<slots.size();++i){auto constraint=constraints.map().canonical(slots[i]);if(!constraint)return fail(constraint.error());SpPreparedEndpoints p;if(!spReadPreparedEndpoints(&body.source(),constraints.kind(),slots[i],&p))return fail(Error::InvalidArgument);auto a=body.map().canonical(uint32_t(p.body_a)),b=body.map().canonical(uint32_t(p.body_b));if(!a||!b)return fail(Error::StaleGeneration);
  size_t count=0;auto storage=bindings.subspan(2*i,2);if(p.has_indices&&p.index_a>=0)storage[count++]={uint32_t(p.index_a),*a};if(p.has_indices&&p.index_b>=0)storage[count++]={uint32_t(p.index_b),*b};if(count==2&&storage[0].native_slot>storage[1].native_slot)std::swap(storage[0],storage[1]);auto map=IdentityMap::prepare(storage.first(count),order.subspan(2*i,2),0x1001);if(!map)return fail(map.error());output[i].emplace(PreparedMap{*constraint,*map});
 }
 return {};
}
}
