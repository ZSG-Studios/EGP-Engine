// SPDX-License-Identifier: MIT
#pragma once
#include "canonical_checkpoint.hpp"
#include <algorithm>
namespace superpos::canonical {
// Process-local bindings are consulted only during admitted capture/restore.
// Their addresses never enter a canonical atom or wire record. The owner must
// keep binding objects alive and freeze both spans until the transaction ends.
struct PointerBinding {void *pointer{};Identity identity{};};
class PointerIdentityMap {
 std::span<const PointerBinding> pointers_;
 std::span<const std::uint32_t> canonical_;
 PointerIdentityMap(std::span<const PointerBinding> p,std::span<const std::uint32_t> c)noexcept:pointers_(p),canonical_(c){}
 static auto address(const void *p)noexcept{return reinterpret_cast<std::uintptr_t>(p);}
public:
 static Result<PointerIdentityMap> prepare(std::span<const PointerBinding> pointers,std::span<std::uint32_t> order,std::uint32_t kind)noexcept{
  if(!kind||pointers.size()>100000||order.size()<pointers.size())return fail(Error::InvalidArgument);
  auto a=address(pointers.data()),b=address(order.data());
  if(pointers.size_bytes()&&order.size_bytes()&&(a<=b?b-a<pointers.size_bytes():a-b<order.size_bytes()))return fail(Error::InvalidArgument);
  for(size_t i=0;i<pointers.size();++i){const auto &p=pointers[i];
   if(!p.pointer||p.identity.kind!=kind||!p.identity.simulation||!p.identity.generation||(i&&address(pointers[i-1].pointer)>=address(p.pointer)))return fail(Error::InvalidArgument);
   order[i]=std::uint32_t(i);
  }
  auto c=order.first(pointers.size());std::sort(c.begin(),c.end(),[pointers](auto a,auto b){return pointers[a].identity.simulation<pointers[b].identity.simulation;});
  for(size_t i=1;i<c.size();++i)if(pointers[c[i-1]].identity.simulation==pointers[c[i]].identity.simulation)return fail(Error::InvalidArgument);
  return PointerIdentityMap(pointers,c);
 }
 Result<Identity> canonical(const void *pointer)const noexcept{
  if(!pointer)return Identity{};
  auto it=std::lower_bound(pointers_.begin(),pointers_.end(),address(pointer),[](const auto &p,auto a){return address(p.pointer)<a;});
  if(it==pointers_.end()||it->pointer!=pointer)return fail(Error::PermissionDenied);return it->identity;
 }
 Result<void *> native(Identity identity)const noexcept{
  if(!identity.kind&&!identity.simulation&&!identity.generation)return static_cast<void *>(nullptr);
  auto it=std::lower_bound(canonical_.begin(),canonical_.end(),identity.simulation,[this](auto i,auto id){return pointers_[i].identity.simulation<id;});
  if(it==canonical_.end()||pointers_[*it].identity!=identity)return fail(Error::StaleGeneration);return pointers_[*it].pointer;
 }
};
}
