// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
namespace superpos::rtc_profile {
// Opaque AF_CONN/upcall identities are never dereferenced. The native uintptr
// representation is an explicit ceiling, including 32-bit providers. No wrap,
// reset or truncation is allowed. A retired identity is never reused.
inline std::optional<std::uint64_t> mint_callback_identity(std::atomic<std::uint64_t>& counter,
    std::uint64_t representation_max)noexcept {
    auto previous=counter.load(std::memory_order_relaxed);
    for(;;){
        if(previous>=representation_max)return std::nullopt;
        const auto next=previous+1;
        if(counter.compare_exchange_weak(previous,next,std::memory_order_relaxed))return next;
    }
}
}
