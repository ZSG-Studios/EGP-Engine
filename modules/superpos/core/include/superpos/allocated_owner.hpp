#pragma once
#include <superpos/allocator.hpp>
#include <new>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace superpos {
// The allocator must outlive this owner. Construction is nonthrowing; any
// fallible initialization belongs to a subsequent typed operation. This owns
// the object's backing, not allocations a provider makes outside its allocator.
template<class Interface> class AllocatedOwner {
 Interface* view_{};
 void* storage_{};
 Allocator* allocator_{};
 void (*destroy_)(void*) noexcept{};
 bool resetting_{};
public:
 AllocatedOwner() noexcept = default;
 ~AllocatedOwner() { reset(); }
 AllocatedOwner(const AllocatedOwner&) = delete;
 AllocatedOwner& operator=(const AllocatedOwner&) = delete;
 AllocatedOwner(AllocatedOwner&& other) noexcept { *this = std::move(other); }
 AllocatedOwner& operator=(AllocatedOwner&& other) noexcept {
  // Destructors may inspect or recursively reset the empty owner, but cannot
  // transfer ownership into or out of it while destruction is in progress.
  if (resetting_ || other.resetting_) std::abort();
  if (this != &other) {
   reset();
   view_ = std::exchange(other.view_, nullptr);
   storage_ = std::exchange(other.storage_, nullptr);
   allocator_ = std::exchange(other.allocator_, nullptr);
   destroy_ = std::exchange(other.destroy_, nullptr);
  }
  return *this;
 }
 template<class Concrete, class... Args>
 static Result<AllocatedOwner> create(Allocator& allocator, MemoryDomain domain, Args&&... args) noexcept {
  static_assert(std::is_convertible_v<Concrete*, Interface*>);
  static_assert(std::is_nothrow_constructible_v<Concrete, Args...>);
  static_assert(std::is_nothrow_destructible_v<Concrete>);
  void* storage = allocator.allocate(sizeof(Concrete), alignof(Concrete), domain);
  if (!storage) return fail(Error::OutOfMemory);
  auto* object = ::new (storage) Concrete(std::forward<Args>(args)...);
  AllocatedOwner result;
  result.view_ = object;
  result.storage_ = storage;
  result.allocator_ = &allocator;
  result.destroy_ = [](void* p) noexcept { static_cast<Concrete*>(p)->~Concrete(); };
  return result;
 }
 void reset() noexcept {
  if (resetting_) return;
  resetting_ = true;
  // Clear the owner before invoking provider destruction; the original concrete
  // address remains charged and is used even when the interface base is offset.
  void* storage = std::exchange(storage_, nullptr);
  auto* allocator = std::exchange(allocator_, nullptr);
  auto destroy = std::exchange(destroy_, nullptr);
  view_ = nullptr;
  if (storage) { destroy(storage); allocator->deallocate(storage); }
  resetting_ = false;
 }
 Interface* get() const noexcept { return view_; }
 Interface& operator*() const noexcept { return *view_; }
 Interface* operator->() const noexcept { return view_; }
 explicit operator bool() const noexcept { return view_ != nullptr; }
 Allocator* backing_allocator() const noexcept { return allocator_; }
};
}
