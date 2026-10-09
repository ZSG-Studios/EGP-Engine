// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/allocator.hpp"
#include "retirement.hpp"
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace superpos::rtc_profile {
// Private exception-using backend only. The borrowed allocator outlives every
// container, object and weak control block. No global hook or implicit fallback.
template<class T> class BackingAllocator {
 template<class> friend class BackingAllocator;
 superpos::Allocator* backing_;
 superpos::MemoryDomain domain_;
public:
 using value_type=T;
 using is_always_equal=std::false_type;
 using propagate_on_container_move_assignment=std::false_type;
 using propagate_on_container_swap=std::false_type;
 BackingAllocator()=delete;
 explicit BackingAllocator(superpos::Allocator& a,superpos::MemoryDomain d=superpos::MemoryDomain::Backend) noexcept : backing_(&a),domain_(d) {}
 template<class U> BackingAllocator(const BackingAllocator<U>& other) noexcept : backing_(other.backing_),domain_(other.domain_) {}
 T* allocate(std::size_t count) {
  if(count>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::bad_alloc();
  auto* raw=backing_->allocate(count*sizeof(T),alignof(T),domain_);
  if(!raw)throw std::bad_alloc();
  return static_cast<T*>(raw);
 }
 void deallocate(T* pointer,std::size_t) noexcept { backing_->deallocate(pointer); }
 template<class U> bool operator==(const BackingAllocator<U>& other) const noexcept { return backing_==other.backing_&&domain_==other.domain_; }
};
template<class T> struct BackedDelete {
 rtc::impl::retirement::Token token;
 BackingAllocator<T> allocator;
 void operator()(T* object) noexcept {
  static_assert(std::is_nothrow_destructible_v<T>);
  std::destroy_at(object);
  allocator.deallocate(object,1);
  rtc::impl::retirement::Domain::deleted(token);
 }
};
// A control block's stored deleter can disappear before its upstream free.
// Standard-library allocator copies/rebinds retain this pin through that free.
template<class T> class PinnedBackingAllocator {
 template<class> friend class PinnedBackingAllocator;
 BackingAllocator<T> allocator_;
 rtc::impl::retirement::Token token_;
public:
 using value_type=T;
 using is_always_equal=std::false_type;
 PinnedBackingAllocator()=delete;
 PinnedBackingAllocator(superpos::Allocator& backing,const rtc::impl::retirement::Token& token) noexcept : allocator_(backing),token_(token) {}
 template<class U> PinnedBackingAllocator(const PinnedBackingAllocator<U>& other) noexcept : allocator_(other.allocator_),token_(other.token_) {}
 T* allocate(std::size_t count) { return allocator_.allocate(count); }
 void deallocate(T* pointer,std::size_t count) noexcept { allocator_.deallocate(pointer,count); }
 template<class U> bool operator==(const PinnedBackingAllocator<U>& other) const noexcept {
  const auto a=token_.key(),b=other.token_.key();
  return allocator_==other.allocator_&&a.domain==b.domain&&a.generation==b.generation&&a.slot==b.slot;
 }
};
// Both concrete object backing and its shared/weak control block are charged.
// Member payloads remain the responsibility of their own allocator contracts.
template<class T,class... Args>
std::shared_ptr<T> make_backed(const rtc::impl::retirement::Token& token,superpos::Allocator& backing,Args&&... args) {
 if(!token)throw rtc::impl::retirement::Failure();
 auto activity=token.enter();if(!activity)throw rtc::impl::retirement::Failure();
 BackingAllocator<T> allocator(backing);
 T* object=allocator.allocate(1);
 try { std::construct_at(object,std::forward<Args>(args)...); }
 catch(...) { allocator.deallocate(object,1);throw; }
 BackedDelete<T> deleter{activity.handoff(),allocator};
 return std::shared_ptr<T>(object,std::move(deleter),PinnedBackingAllocator<std::byte>(backing,token));
}
}
