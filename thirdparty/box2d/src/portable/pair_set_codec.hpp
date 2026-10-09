// SPDX-License-Identifier: MIT
#pragma once
#include "tree_node_codec.hpp"
extern "C" {
#include "table.h"
}
namespace superpos::box2d_portable {
constexpr uint32_t pair_set_kind=0x1E00;
struct PairSetImage {
 canonical::Atom capacity{};std::span<canonical::Atom> storage;
 std::array<canonical::Field,2> fields{};canonical::Record record{};
 explicit PairSetImage(std::span<canonical::Atom>)noexcept;
 PairSetImage(const PairSetImage &)=delete;PairSetImage &operator=(const PairSetImage &)=delete;
};
std::span<const canonical::FieldSpec> pair_set_fields()noexcept;
Status capture_pair_set(const b2HashSet &,canonical::Identity,const canonical::IdentityMap &,PairSetImage &)noexcept;
// Uses native b2AddKey only after reserving the whole native table; never transfers ownership.
Result<b2HashSet> restore_pair_set(const canonical::Record &,const canonical::IdentityMap &,std::span<uint64_t> key_staging,std::span<b2SetItem> item_staging)noexcept;
}
