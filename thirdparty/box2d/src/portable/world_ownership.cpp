// SPDX-License-Identifier: MIT
#include "world_ownership.hpp"
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Range{const void*p;size_t bytes;};
bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.bytes&&b.bytes&&(x<=y?y-x<a.bytes:x-y<b.bytes);}
constexpr uint32_t kinds[]={0x1001,0x1006,0x1008,0x1402,0x1401};
}
Result<WorldOwnerMap> derive_world_owner_map(const SpWorldOwnershipView&view,SpOwnerKind kind,LifetimeRegistry&registry,RegistryLease lease,std::span<const int>free_ids,WorldMapStorage s)noexcept{
 if(kind<SP_OWNER_BODY||kind>SP_OWNER_SET||registry.kind()!=kinds[kind])return fail(Error::InvalidArgument);
 const uint32_t counts[]={view.body_count,view.contact_count,view.joint_count,view.island_count,view.set_count};const size_t count=counts[kind];
 if(count>s.native.size()||count>s.marks.size())return fail(Error::CapacityExceeded);
 const Range writes[]={{s.native.data(),s.native.size_bytes()},{s.marks.data(),s.marks.size_bytes()},{s.bindings.data(),s.bindings.size_bytes()},{s.order.data(),s.order.size_bytes()}};
 for(size_t i=0;i<4;++i){if(registry.overlaps_storage(writes[i].p,writes[i].bytes)||spWorldOwnershipOverlaps(&view,writes[i].p,writes[i].bytes)||overlap(writes[i],{free_ids.data(),free_ids.size_bytes()}))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 if(!spValidateWorldOwnership(&view))return fail(Error::InvalidArgument);
 for(uint32_t i=0;i<count;++i){SpNativeLifetime n;if(!spObserveWorldOwner(&view,kind,i,&n))return fail(Error::InvalidArgument);s.native[i]={n.native_id,n.generation};}
 auto map=registry.derive(lease,s.native.first(count),free_ids,s.marks,s.bindings,s.order);if(!map)return fail(map.error());return WorldOwnerMap(*map,view,kind,registry,lease);
}
Result<IdentityMap> derive_current_solver_body_map(const WorldOwnerMap&owner,uint32_t set,std::span<int>ids,std::span<NativeBinding>bindings,std::span<uint32_t>order)noexcept{
 if(!owner.lease_active())return fail(Error::NotReady);if(ids.size()>100000)return fail(Error::CapacityExceeded);if(owner.kind()!=SP_OWNER_BODY)return fail(Error::InvalidArgument);const auto&view=owner.source();const Range writes[]={{ids.data(),ids.size_bytes()},{bindings.data(),bindings.size_bytes()},{order.data(),order.size_bytes()}};
 for(size_t i=0;i<3;++i){if(owner.map().overlaps_storage(writes[i].p,writes[i].bytes)||overlap(writes[i],{&owner,sizeof(owner)})||spWorldOwnershipOverlaps(&view,writes[i].p,writes[i].bytes))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 if(!spValidateWorldOwnership(&view))return fail(Error::InvalidArgument);uint32_t count=0;if(!spCopyWorldSolverBodies(&view,set,ids.data(),uint32_t(ids.size()),&count))return fail(Error::CapacityExceeded);if(bindings.size()<count||order.size()<count)return fail(Error::CapacityExceeded);
 for(uint32_t i=0;i<count;++i){auto id=owner.map().canonical(uint32_t(ids[i]));if(!id)return fail(id.error());bindings[i]={i,*id};}return IdentityMap::prepare(bindings.first(count),order.first(count),kinds[SP_OWNER_BODY]);
}
}
