// SPDX-License-Identifier: MIT
#include "world_pool_slots.hpp"
#include <algorithm>
namespace superpos::box2d_portable {using namespace canonical;
namespace {struct Range{const void*p;size_t bytes;};bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.bytes&&b.bytes&&(x<=y?y-x<a.bytes:x-y<b.bytes);}}
bool WorldPoolSlots::overlaps_storage(const void*p,size_t bytes)const noexcept{return overlap({p,bytes},{this,sizeof(*this)})||overlap({p,bytes},{source_,sizeof(*source_)})||registry_->overlaps_storage(p,bytes)||map_.overlaps_storage(p,bytes)||spWorldCaptureOverlaps(source_->source,p,bytes);}
Result<WorldPoolSlots> derive_world_pool_slots(const SpWorldCaptureView&v,SpWorldPool pool,LifetimeRegistry&r,RegistryLease lease,WorldMapStorage s)noexcept{
 if(pool<SP_POOL_BODY||pool>=SP_POOL_COUNT||r.kind()!=world_pool_slot_kind(pool)||!spWorldCaptureMatches(&v))return fail(Error::InvalidArgument);if(!r.lease_active(lease))return fail(Error::NotReady);
 const auto&native=v.pools[pool];if(native.allocated_count>100000||native.allocated_count>s.native.size()||native.allocated_count>s.marks.size()||native.free_count>native.allocated_count||native.free_count>native.free_capacity||(!native.free_entries&&native.free_capacity))return fail(Error::CapacityExceeded);
 const Range writes[]={{s.native.data(),s.native.size_bytes()},{s.marks.data(),s.marks.size_bytes()},{s.bindings.data(),s.bindings.size_bytes()},{s.order.data(),s.order.size_bytes()}};
 for(size_t i=0;i<4;++i){if(r.overlaps_storage(writes[i].p,writes[i].bytes)||overlap(writes[i],{&v,sizeof(v)})||spWorldCaptureOverlaps(v.source,writes[i].p,writes[i].bytes))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 std::fill_n(s.marks.begin(),native.allocated_count,uint8_t{});
 for(uint32_t i=0;i<native.free_count;++i){int n=native.free_entries[i];if(n<0||uint32_t(n)>=native.allocated_count||s.marks[size_t(n)])return fail(Error::InvalidArgument);s.marks[size_t(n)]=1;}
 for(uint32_t i=0;i<native.allocated_count;++i){SpNativeLifetime actual{};if(!spObserveWorldCaptureSlot(&v,pool,i,&actual)||((actual.native_id==-1)!=(s.marks[i]!=0))||(actual.native_id!=-1&&actual.native_id!=int(i)))return fail(Error::InvalidArgument);s.native[i]={int(i),0};}
 auto map=r.derive(lease,s.native.first(native.allocated_count),{},s.marks,s.bindings,s.order);if(!map)return fail(map.error());return WorldPoolSlots(*map,v,r,lease,pool);
}
Status capture_world_pool(const WorldPoolSlots&slots,Identity id,std::span<std::byte>visited,PoolImage&image)noexcept{
 if(!slots.lease_active()||!spWorldCaptureMatches(&slots.source()))return fail(Error::NotReady);auto definition=pool_definition(world_pool_slot_kind(slots.pool()),100000);if(!definition)return fail(definition.error());
 const Range writes[]={{visited.data(),visited.size_bytes()},{&image,sizeof(image)},{image.free_order_storage.data(),image.free_order_storage.size_bytes()}};
 for(size_t i=0;i<3;++i){if(slots.overlaps_storage(writes[i].p,writes[i].bytes))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 return capture_pool(slots.source().pools[slots.pool()],id,*definition,slots.map(),visited,image);
}
}
