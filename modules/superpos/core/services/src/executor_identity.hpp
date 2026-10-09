#pragma once
#include "superpos/result.hpp"
#include <atomic>
#include <cstdint>
namespace superpos::detail {
// Process-local identity; does not authorize work or cross a network boundary.
// Failed construction may burn an identity. Neither zero nor exhaustion wraps.
inline Result<std::uint64_t> reserve_executor_identity(std::atomic<std::uint64_t>& next) noexcept {
    auto current=next.load(std::memory_order_relaxed);
    for(;;){
        if(!current||current==UINT64_MAX)return fail(Error::CounterExhausted);
        if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed,std::memory_order_relaxed))return current;
    }
}
}
