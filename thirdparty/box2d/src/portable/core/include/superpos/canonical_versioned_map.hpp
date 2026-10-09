// SPDX-License-Identifier: MIT
#pragma once
#include "canonical_checkpoint.hpp"
#include <algorithm>
namespace superpos::canonical {
struct NativeVersion {std::uint32_t slot{},generation{};bool operator==(const NativeVersion &)const noexcept=default;};
struct VersionedNativeBinding {NativeVersion native;Identity identity;};
// Retained native generations need distinct lifetime identities. Historical
// bindings stay charged/alive until all referring checkpoints are retired.
// Native generation zero is excluded: the owner must retire native slots before
// their bounded native generation wraps, then assign a fresh canonical lifetime.
class VersionedIdentityMap {
 std::span<const VersionedNativeBinding> bindings_;
 std::span<const std::uint32_t> canonical_;
 VersionedIdentityMap(std::span<const VersionedNativeBinding> b,std::span<const std::uint32_t> c)noexcept:bindings_(b),canonical_(c){}
 static bool less(NativeVersion a,NativeVersion b)noexcept{return a.slot<b.slot||(a.slot==b.slot&&a.generation<b.generation);}
public:
 static Result<VersionedIdentityMap> prepare(std::span<const VersionedNativeBinding> bindings,std::span<std::uint32_t> order,std::uint32_t kind,std::uint32_t maximum_native_generation)noexcept{
  if(!kind||!maximum_native_generation||bindings.size()>100000||order.size()<bindings.size())return fail(Error::InvalidArgument);
  auto a=reinterpret_cast<std::uintptr_t>(bindings.data()),b=reinterpret_cast<std::uintptr_t>(order.data());if(bindings.size_bytes()&&order.size_bytes()&&(a<=b?b-a<bindings.size_bytes():a-b<order.size_bytes()))return fail(Error::InvalidArgument);
  for(size_t i=0;i<bindings.size();++i){const auto &v=bindings[i];if(!v.native.generation||v.native.generation>maximum_native_generation||v.identity.kind!=kind||!v.identity.simulation||!v.identity.generation||(i&&!less(bindings[i-1].native,v.native)))return fail(Error::InvalidArgument);order[i]=std::uint32_t(i);}
  auto index=order.first(bindings.size());if(index.size()>1)std::sort(index.begin(),index.end(),[bindings](auto a,auto b){return bindings[a].identity.simulation<bindings[b].identity.simulation;});
  for(size_t i=1;i<index.size();++i)if(bindings[index[i-1]].identity.simulation==bindings[index[i]].identity.simulation)return fail(Error::InvalidArgument);
  return VersionedIdentityMap(bindings,index);
 }
 Result<Identity> canonical(NativeVersion native)const noexcept{
  auto i=std::lower_bound(bindings_.begin(),bindings_.end(),native,[](const auto &a,NativeVersion b){return less(a.native,b);});if(i==bindings_.end()||i->native!=native)return fail(Error::StaleGeneration);return i->identity;
 }
 Result<NativeVersion> native(Identity identity)const noexcept{
  auto i=std::lower_bound(canonical_.begin(),canonical_.end(),identity.simulation,[this](auto a,auto b){return bindings_[a].identity.simulation<b;});if(i==canonical_.end()||bindings_[*i].identity!=identity)return fail(Error::StaleGeneration);return bindings_[*i].native;
 }
};
}
