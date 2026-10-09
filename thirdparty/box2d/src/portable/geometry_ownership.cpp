// SPDX-License-Identifier: MIT
#include "geometry_ownership.hpp"
#include "shape_codec.hpp"
#include "chain_codec.hpp"
#include <algorithm>
namespace superpos::box2d_portable {using namespace canonical;
namespace {
struct Range{const void*p;size_t bytes;};
bool overlap(Range a,Range b)noexcept{auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.bytes&&b.bytes&&(x<=y?y-x<a.bytes:x-y<b.bytes);}
}
bool GeometryOwnership::overlaps_storage(const void*p,size_t bytes)const noexcept{
 return overlap({p,bytes},{this,sizeof(*this)})||overlap({p,bytes},{bodies_,sizeof(*bodies_)})||bodies_->overlaps_storage(p,bytes)||spGeometryOwnershipOverlaps(source_,p,bytes);
}
Result<GeometryOwnership> validate_geometry_ownership(const SpGeometryOwnershipView&view,const WorldOwnerMap&bodies,GeometryOwnershipScratch s)noexcept{
 if(!bodies.lease_active())return fail(Error::NotReady);
 if(bodies.kind()!=SP_OWNER_BODY||view.world!=&bodies.source())return fail(Error::InvalidArgument);
 if(s.marks.size()>100000||s.contact_keys.size()>100000||s.pair_keys.size()>100000||s.contact_keys.size()!=s.pair_keys.size())return fail(Error::CapacityExceeded);
 const Range writes[]={{s.marks.data(),s.marks.size_bytes()},{s.contact_keys.data(),s.contact_keys.size_bytes()},{s.pair_keys.data(),s.pair_keys.size_bytes()}};
 for(size_t i=0;i<3;++i){if(bodies.overlaps_storage(writes[i].p,writes[i].bytes)||overlap(writes[i],{&bodies,sizeof(bodies)})||spGeometryOwnershipOverlaps(&view,writes[i].p,writes[i].bytes))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 uint32_t count=0;if(!spValidateGeometryOwnership(&view,s.marks.data(),uint32_t(s.marks.size()),s.contact_keys.data(),s.pair_keys.data(),uint32_t(s.contact_keys.size()),&count))return fail(Error::InvalidArgument);
 auto contacts=s.contact_keys.first(count),pairs=s.pair_keys.first(count);std::sort(contacts.begin(),contacts.end());std::sort(pairs.begin(),pairs.end());
 for(size_t i=0;i<count;++i)if(contacts[i]!=pairs[i]||(i&&contacts[i]==contacts[i-1]))return fail(Error::InvalidArgument);
 return GeometryOwnership(view,bodies);
}
Result<GeometryOwnerMap> derive_geometry_map(const GeometryOwnership&owner,bool chain,LifetimeRegistry&registry,RegistryLease lease,std::span<const int>free_ids,WorldMapStorage s)noexcept{
 if(!owner.lease_active()||!registry.lease_active(lease))return fail(Error::NotReady);
 if(registry.kind()!=(chain?chain_kind:shape_kind))return fail(Error::InvalidArgument);
 const auto&view=owner.source();const uint32_t count=chain?view.chain_count:view.shape_count;
 if(count>s.native.size()||count>s.marks.size())return fail(Error::CapacityExceeded);
 const Range writes[]={{s.native.data(),s.native.size_bytes()},{s.marks.data(),s.marks.size_bytes()},{s.bindings.data(),s.bindings.size_bytes()},{s.order.data(),s.order.size_bytes()}};
 for(size_t i=0;i<4;++i){if(owner.overlaps_storage(writes[i].p,writes[i].bytes)||registry.overlaps_storage(writes[i].p,writes[i].bytes)||overlap(writes[i],{free_ids.data(),free_ids.size_bytes()}))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlap(writes[i],writes[j]))return fail(Error::InvalidArgument);}
 for(uint32_t i=0;i<count;++i){SpNativeLifetime n{};if(!spObserveGeometryOwner(&view,chain,i,&n))return fail(Error::InvalidArgument);s.native[i]={n.native_id,n.generation};}
 auto map=registry.derive(lease,s.native.first(count),free_ids,s.marks,s.bindings,s.order);if(!map)return fail(map.error());return GeometryOwnerMap(*map,owner,registry,lease,chain);
}
bool GeometryOwnerMap::overlaps_storage(const void*p,size_t bytes)const noexcept{return overlap({p,bytes},{this,sizeof(*this)})||owner_->overlaps_storage(p,bytes)||registry_->overlaps_storage(p,bytes)||map_.overlaps_storage(p,bytes);}
Result<IdentityMap> derive_shape_proxy_map(const GeometryOwnerMap&owner,uint32_t type,std::span<NativeBinding>bindings,std::span<uint32_t>order)noexcept{
 if(!owner.lease_active())return fail(Error::NotReady);if(owner.chain()||type>2)return fail(Error::InvalidArgument);
 const Range writes[]={{bindings.data(),bindings.size_bytes()},{order.data(),order.size_bytes()}};
 if(overlap(writes[0],writes[1]))return fail(Error::InvalidArgument);for(auto w:writes)if(owner.overlaps_storage(w.p,w.bytes))return fail(Error::InvalidArgument);
 uint32_t count=0;const auto&v=owner.source();for(uint32_t i=0;i<v.shape_count;++i){SpNativeLifetime n{};if(!spObserveGeometryOwner(&v,false,i,&n))return fail(Error::InvalidArgument);if(n.native_id==-1)continue;int node,tree;if(!spObserveShapeProxy(&v,i,&node,&tree))return fail(Error::InvalidArgument);if(tree!=(int)type)continue;if(count>=bindings.size()||count>=order.size())return fail(Error::CapacityExceeded);auto id=owner.map().canonical(i);if(!id)return fail(id.error());id->kind=shape_proxy_kind;bindings[count++]={uint32_t(node),*id};}
 auto active=bindings.first(count);std::sort(active.begin(),active.end(),[](const NativeBinding&a,const NativeBinding&b){return a.native_slot<b.native_slot;});
 return IdentityMap::prepare(active,order.first(count),shape_proxy_kind);
}
}
