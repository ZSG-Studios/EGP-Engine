// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/allocator.hpp"
#include <limits>
#include <memory>
#include <span>
#include <type_traits>

namespace superpos_egp {
// Private C++23 storage. Allocation is fallible, charged, and completed before
// constructing elements. The allocator must outlive the array.
template<class T> class ChargedArray {
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    superpos::Allocator &allocator;
    superpos::MemoryDomain domain;
    T *storage = nullptr;
    size_t count = 0;
public:
    ChargedArray(superpos::Allocator &p_allocator, superpos::MemoryDomain p_domain) noexcept : allocator(p_allocator), domain(p_domain) {}
    ~ChargedArray() { clear(); }
    ChargedArray(const ChargedArray &) = delete;
    ChargedArray &operator=(const ChargedArray &) = delete;
    superpos::Status initialize(size_t p_count) noexcept {
        if (storage) { return superpos::fail(superpos::Error::Busy); }
        if (p_count > std::numeric_limits<size_t>::max() / sizeof(T)) { return superpos::fail(superpos::Error::Overflow); }
        if (!p_count) { return {}; }
        auto *candidate = static_cast<T *>(allocator.allocate(p_count * sizeof(T), alignof(T), domain));
        if (!candidate) { return superpos::fail(superpos::Error::OutOfMemory); }
        for (size_t i = 0; i < p_count; ++i) { std::construct_at(candidate + i); }
        storage = candidate;
        count = p_count;
        return {};
    }
    void clear() noexcept {
        if (!storage) { return; }
        for (size_t i = count; i; --i) { std::destroy_at(storage + i - 1); }
        allocator.deallocate(storage);
        storage = nullptr;
        count = 0;
    }
    std::span<T> span() noexcept { return {storage, count}; }
    std::span<const T> span() const noexcept { return {storage, count}; }
    T &operator[](size_t position) noexcept { return storage[position]; }
    size_t size() const noexcept { return count; }
};
}
