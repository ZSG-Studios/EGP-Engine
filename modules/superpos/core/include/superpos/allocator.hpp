#pragma once
#include "result.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <span>
namespace superpos {
enum class MemoryDomain : unsigned { World, Replication, Session, History, Recovery, Backend, Count };
struct MemoryPlan {
 std::array<std::size_t, 6> limits{64ull<<20,256ull<<20,256ull<<20,128ull<<20,512ull<<20,64ull<<20};
 static MemoryPlan client() noexcept { return {{8ull<<20,24ull<<20,16ull<<20,8ull<<20,4ull<<20,4ull<<20}}; }
 // A caller can additionally enforce its declared complete-owned-memory ceiling.
 Status validate(std::size_t maximum_owned_bytes = SIZE_MAX) const noexcept;
};
class Allocator {
public:
 virtual ~Allocator() = default;
 virtual void* allocate(std::size_t size, std::size_t alignment, MemoryDomain domain) noexcept = 0;
 virtual void deallocate(void* pointer) noexcept = 0;
};
class BudgetAllocator final : public Allocator {
 MemoryPlan plan_;
 bool valid_;
 std::array<std::atomic_size_t, 6> used_{};
public:
 explicit BudgetAllocator(MemoryPlan plan = {}) noexcept : plan_(plan), valid_(bool(plan.validate())) {}
 Status configuration_status() const noexcept { return plan_.validate(); }
 // Exact owned backing charge, including private metadata and alignment slack.
 static Result<std::size_t> allocation_charge(std::size_t size, std::size_t alignment) noexcept;
 void* allocate(std::size_t size, std::size_t alignment, MemoryDomain domain) noexcept override;
 void deallocate(void* pointer) noexcept override;
 std::size_t used(MemoryDomain d) const noexcept { return used_[static_cast<unsigned>(d)].load(); }
 std::size_t total() const noexcept;
};
// A per-owner quota charged against one shared host backing allocator. Bind only
// while externally quiescent; both allocators must outlive all returned blocks.
class QuotaAllocator final : public Allocator {
 BudgetAllocator* parent_{};
 MemoryPlan plan_{};
 std::array<std::atomic_size_t, 6> used_{};
public:
 QuotaAllocator() noexcept = default;
 ~QuotaAllocator();
 QuotaAllocator(const QuotaAllocator&) = delete;
 QuotaAllocator& operator=(const QuotaAllocator&) = delete;
 Status bind(BudgetAllocator& parent, MemoryPlan plan) noexcept;
 void* allocate(std::size_t size, std::size_t alignment, MemoryDomain domain) noexcept override;
 void deallocate(void* pointer) noexcept override;
 std::size_t used(MemoryDomain d) const noexcept;
 std::size_t total() const noexcept;
};
class Buffer {
 Allocator* allocator_{};
 MemoryDomain domain_{};
 std::byte* data_{};
 std::size_t size_{}, capacity_{};
public:
 explicit Buffer(Allocator& a, MemoryDomain d = MemoryDomain::Session) noexcept : allocator_(&a), domain_(d) {}
 ~Buffer() { if (data_) allocator_->deallocate(data_); }
 Buffer(const Buffer&) = delete;
 Buffer& operator=(const Buffer&) = delete;
 Buffer(Buffer&& other) noexcept;
 Buffer& operator=(Buffer&& other) noexcept;
 Status reserve(std::size_t size) noexcept;
 Status resize(std::size_t size) noexcept;
 std::span<std::byte> bytes() noexcept { return {data_,size_}; }
 std::span<const std::byte> bytes() const noexcept { return {data_,size_}; }
 std::size_t size() const noexcept { return size_; }
 std::size_t capacity() const noexcept { return capacity_; }
 void clear() noexcept { size_=0; }
};
}
