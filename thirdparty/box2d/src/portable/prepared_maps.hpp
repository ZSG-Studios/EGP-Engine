// SPDX-License-Identifier: MIT
#pragma once
#include "world_ownership.hpp"
#include "prepared_map_bridge.h"
#include <optional>
namespace superpos::box2d_portable {
struct PreparedMap {canonical::Identity constraint;canonical::IdentityMap indices;};
// Batch boundary validates native/map/registry overlap once. Failure may leave
// private outputs partially filled; discard the entire candidate on failure.
Status derive_prepared_maps(const WorldOwnerMap&body,const WorldOwnerMap&constraints,
 std::span<const uint32_t> native_slots,std::span<canonical::NativeBinding> bindings,
 std::span<uint32_t> order,std::span<std::optional<PreparedMap>> output)noexcept;
}
