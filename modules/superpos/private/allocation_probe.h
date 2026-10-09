// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace superpos_egp {
enum class AllocationPoint : unsigned { Records, Views, WorldArena, SchemaManifest, ObjectCopy, PacketCopy, Count };
struct AllocationDiagnostics {
    std::array<uint64_t, size_t(AllocationPoint::Count)> live{}, allocated{}, denied{};
};
// Isolated native qualification only; no ClassDB or generated SDK entry.
bool arm_allocation_failure(AllocationPoint) noexcept;
bool consume_allocation_failure(AllocationPoint) noexcept;
void allocation_acquired(AllocationPoint) noexcept;
void allocation_released(AllocationPoint) noexcept;
AllocationDiagnostics allocation_diagnostics() noexcept;
}
