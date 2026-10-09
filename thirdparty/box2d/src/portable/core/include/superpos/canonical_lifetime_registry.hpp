// SPDX-License-Identifier: MIT
#pragma once
#include "canonical_identity_map.hpp"
#include <atomic>
namespace superpos::canonical {
enum class LifetimePhase:uint8_t { Free, Reserved, Live, Retired };
struct LifetimeSlot { Identity identity{};uint64_t serial{};uint32_t native_generation{};LifetimePhase phase=LifetimePhase::Free; };
struct BirthTicket {Identity identity{};uint64_t serial{};uint32_t native_slot{},native_generation{};const void*owner{};uint64_t registry_identity{};};
struct RegistryLease {uint64_t serial{};const void*owner{};uint64_t registry_identity{};};
struct NativeLifetime {int native_id=-1;uint32_t generation{};};
class LifetimeRegistry {
 std::span<LifetimeSlot> slots_;uint32_t kind_{},native_max_{};uint64_t next_identity_=1,serial_{},registry_identity_{};bool identity_exhausted_{},leased_{};
 // This process-component domain outlives its registries. Unloading the code
 // invalidates every token; token use across module unload is unsupported.
 inline static std::atomic<uint64_t> next_registry_identity_{1};
 static Result<uint64_t> claim_identity()noexcept{auto n=next_registry_identity_.load(std::memory_order_relaxed);for(;;){if(!n)return fail(Error::CapacityExceeded);auto after=n==UINT64_MAX?0:n+1;if(next_registry_identity_.compare_exchange_weak(n,after,std::memory_order_relaxed))return n;}}
 LifetimeRegistry(std::span<LifetimeSlot>s,uint32_t kind,uint32_t max,uint64_t next,uint64_t registry)noexcept:slots_(s),kind_(kind),native_max_(max),next_identity_(next),registry_identity_(registry){}
 bool overlap(const void*p,size_t n)const noexcept {auto x=reinterpret_cast<uintptr_t>(p),y=reinterpret_cast<uintptr_t>(slots_.data()),z=reinterpret_cast<uintptr_t>(this);return n&&((slots_.size_bytes()&&(x<=y?y-x<n:x-y<slots_.size_bytes()))||(x<=z?z-x<n:x-z<sizeof(*this)));}
public:
 uint32_t kind()const noexcept{return kind_;}
 bool lease_active(RegistryLease l)const noexcept{return l.owner==slots_.data()&&l.registry_identity==registry_identity_&&leased_&&l.serial==serial_;}
 bool overlaps_storage(const void*p,size_t bytes)const noexcept{return overlap(p,bytes);}
 LifetimeRegistry(const LifetimeRegistry&)=delete;
 LifetimeRegistry&operator=(const LifetimeRegistry&)=delete;
 LifetimeRegistry&operator=(LifetimeRegistry&&)=delete;
 // Exclusive backing storage must outlive the registry, maps and tickets.
 // Moving transfers that owner; moved-from operations fail.
 LifetimeRegistry(LifetimeRegistry&&other)noexcept:slots_(other.slots_),kind_(other.kind_),native_max_(other.native_max_),next_identity_(other.next_identity_),serial_(other.serial_),registry_identity_(other.registry_identity_),identity_exhausted_(other.identity_exhausted_),leased_(other.leased_){other.slots_={};other.kind_=0;other.leased_=false;}
 static Result<LifetimeRegistry> create(std::span<LifetimeSlot>slots,uint32_t kind,uint32_t native_bits,uint64_t first_identity=1)noexcept{
  if(slots.empty()||slots.size()>100000||!kind||!first_identity||(native_bits!=0&&native_bits!=16&&native_bits!=32))return fail(Error::InvalidArgument);
  auto registry=claim_identity();if(!registry)return fail(registry.error());for(auto&s:slots)s={};return LifetimeRegistry(slots,kind,native_bits==32?UINT32_MAX:native_bits==16?UINT16_MAX:0,first_identity,*registry);
 }
 Result<BirthTicket> reserve(uint32_t slot)noexcept{
  if(leased_)return fail(Error::NotReady);if(slot>=slots_.size())return fail(Error::InvalidArgument);auto&s=slots_[slot];if(s.phase!=LifetimePhase::Free)return fail(Error::NotReady);
  if(identity_exhausted_||serial_==UINT64_MAX||s.identity.generation==UINT64_MAX||(native_max_&&s.native_generation==native_max_))return fail(Error::CapacityExceeded);
  ++serial_;Identity id{kind_,next_identity_,s.identity.generation+1};if(next_identity_==UINT64_MAX)identity_exhausted_=true;else ++next_identity_;s.identity=id;s.serial=serial_;s.phase=LifetimePhase::Reserved;return BirthTicket{id,serial_,slot,native_max_?s.native_generation+1:0,slots_.data(),registry_identity_};
 }
 Status commit(const BirthTicket&t,NativeLifetime actual)noexcept{
  if(leased_)return fail(Error::NotReady);if((t.owner!=slots_.data()||t.registry_identity!=registry_identity_))return fail(Error::StaleGeneration);if(t.native_slot>=slots_.size())return fail(Error::InvalidArgument);auto&s=slots_[t.native_slot];if(s.phase!=LifetimePhase::Reserved||s.identity!=t.identity||s.serial!=t.serial)return fail(Error::StaleGeneration);
  if(t.native_generation!=(native_max_?s.native_generation+1:0)||actual.native_id<0||uint32_t(actual.native_id)!=t.native_slot||actual.generation!=t.native_generation)return fail(Error::InvalidArgument);s.native_generation=actual.generation;s.phase=LifetimePhase::Live;return {};
 }
 Status abort(const BirthTicket&t)noexcept{if(leased_)return fail(Error::NotReady);if((t.owner!=slots_.data()||t.registry_identity!=registry_identity_))return fail(Error::StaleGeneration);if(t.native_slot>=slots_.size())return fail(Error::InvalidArgument);auto&s=slots_[t.native_slot];if(s.phase!=LifetimePhase::Reserved||s.identity!=t.identity||s.serial!=t.serial)return fail(Error::StaleGeneration);s.phase=LifetimePhase::Free;return {};}
 Status retire(uint32_t slot,Identity id,NativeLifetime actual)noexcept{
  if(leased_)return fail(Error::NotReady);if(slot>=slots_.size())return fail(Error::InvalidArgument);auto&s=slots_[slot];if(s.phase!=LifetimePhase::Live||s.identity!=id)return fail(Error::StaleGeneration);if(actual.native_id!=-1||actual.generation!=s.native_generation)return fail(Error::InvalidArgument);s.phase=s.identity.generation==UINT64_MAX||(native_max_&&s.native_generation==native_max_)?LifetimePhase::Retired:LifetimePhase::Free;return {};
 }
 Result<RegistryLease> lease()noexcept{if(slots_.empty()||leased_||serial_==UINT64_MAX)return fail(Error::NotReady);++serial_;leased_=true;return RegistryLease{serial_,slots_.data(),registry_identity_};}
 Status release(RegistryLease l)noexcept{if((l.owner!=slots_.data()||l.registry_identity!=registry_identity_)||!leased_||l.serial!=serial_)return fail(Error::StaleGeneration);leased_=false;return {};}
 // Pool slots are dense; free order is retained by the separate pool codec.
 // This validates each native slot's current lifetime before deriving a map.
 Result<IdentityMap> derive(RegistryLease l,std::span<const NativeLifetime>native,std::span<const int>free_ids,std::span<uint8_t>marks,std::span<NativeBinding>bindings,std::span<uint32_t>order)const noexcept{
  if((l.owner!=slots_.data()||l.registry_identity!=registry_identity_)||!leased_||l.serial!=serial_)return fail(Error::NotReady);if(native.size()>slots_.size()||marks.size()<native.size())return fail(Error::CapacityExceeded);
  struct R{const void*p;size_t n;};const R reads[]={{native.data(),native.size_bytes()},{free_ids.data(),free_ids.size_bytes()}},writes[]={{marks.data(),marks.size_bytes()},{bindings.data(),bindings.size_bytes()},{order.data(),order.size_bytes()}};
  auto overlaps=[](R a,R b){auto x=reinterpret_cast<uintptr_t>(a.p),y=reinterpret_cast<uintptr_t>(b.p);return a.n&&b.n&&(x<=y?y-x<a.n:x-y<b.n);};for(size_t i=0;i<3;++i){if(overlap(writes[i].p,writes[i].n))return fail(Error::InvalidArgument);for(auto r:reads)if(overlaps(writes[i],r))return fail(Error::InvalidArgument);for(size_t j=0;j<i;++j)if(overlaps(writes[i],writes[j]))return fail(Error::InvalidArgument);}
  std::fill(marks.begin(),marks.begin()+native.size(),uint8_t(0));for(int id:free_ids){if(id<0||size_t(id)>=native.size()||marks[id])return fail(Error::InvalidArgument);marks[id]=1;}
  size_t count=0;for(size_t i=0;i<native.size();++i){const auto&s=slots_[i];const auto&n=native[i];if(marks[i]){if(n.native_id!=-1||(s.phase!=LifetimePhase::Free&&s.phase!=LifetimePhase::Retired)||n.generation!=s.native_generation)return fail(Error::StaleGeneration);}else{if(n.native_id<0||size_t(n.native_id)!=i||s.phase!=LifetimePhase::Live||n.generation!=s.native_generation)return fail(Error::StaleGeneration);++count;}}
  for(size_t i=native.size();i<slots_.size();++i)if(slots_[i].phase==LifetimePhase::Live||slots_[i].phase==LifetimePhase::Reserved)return fail(Error::InvalidArgument);
  if(bindings.size()<count||order.size()<count)return fail(Error::CapacityExceeded);size_t cursor=0;for(size_t i=0;i<native.size();++i)if(!marks[i])bindings[cursor++]={uint32_t(i),slots_[i].identity};return IdentityMap::prepare(bindings.first(count),order.first(count),kind_);
 }
};
}
