#pragma once
#include <superpos/allocator.hpp>
namespace superpos_egp {
// Private module parent; available before Session construction and never
// destroyed by static teardown. All deferred owners must retire explicitly.
superpos::BudgetAllocator& module_backing() noexcept;
superpos::Status module_memory_configuration() noexcept;
std::size_t module_static_bytes() noexcept;
std::size_t module_aggregate_bytes() noexcept;
}
