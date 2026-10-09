// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/canonical_checkpoint.hpp>
#include <superpos/canonical_identity_map.hpp>
#include "pool_bridge.h"
#include <array>
namespace superpos::box2d_portable {
// Every allocated slot, including free slots, must have a stable token in the
// supplied map. Token generations are full-width and belong to the canonical
// identity namespace; native free-list indices are never transmitted.
struct PoolImage {
 std::span<canonical::Atom> free_order_storage;
 std::array<canonical::Atom,2> counts{};
 std::array<canonical::Field,3> fields{};
 canonical::Record record{};
 explicit PoolImage(std::span<canonical::Atom> free_order) noexcept;
 PoolImage(const PoolImage &)=delete;PoolImage &operator=(const PoolImage &)=delete;
};
struct PoolDefinition {
 std::array<canonical::FieldSpec,3> fields{};
 std::uint32_t record_kind{},token_kind{},maximum_slots{};
};
Result<PoolDefinition> pool_definition(std::uint32_t token_kind,std::uint32_t maximum_slots) noexcept;
Status capture_pool(const SpPoolView &,canonical::Identity,const PoolDefinition &,const canonical::IdentityMap &,
 std::span<std::byte> visited,PoolImage &) noexcept;
// Private candidate output. Staging integers and visited storage are charged by
// the caller; failures leave the caller's actual native pool unpublished.
Result<SpPoolView> restore_pool(const canonical::Record &,const PoolDefinition &,const canonical::IdentityMap &,
 std::span<std::byte> visited,std::span<int> native_staging) noexcept;
}
