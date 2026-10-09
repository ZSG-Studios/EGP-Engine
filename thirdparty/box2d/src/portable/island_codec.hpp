// SPDX-License-Identifier: MIT
#pragma once
#include "cold_joint_codec.hpp"
#include "island_bridge.h"
namespace superpos::box2d_portable {
struct IslandMappings {const canonical::IdentityMap &island,&solver_set,&set_member,&body,&contact,&joint;};
// All spans are independently charged and kept alive through encode/publication.
struct IslandImage {
 std::span<canonical::Atom> body_storage,contact_storage,joint_storage;
 std::array<canonical::Atom,7> scalars{};std::array<canonical::Field,14> fields{};canonical::Record record{};
 IslandImage(std::span<canonical::Atom>,std::span<canonical::Atom>,std::span<canonical::Atom>)noexcept;
 IslandImage(const IslandImage &)=delete;IslandImage &operator=(const IslandImage &)=delete;
};
std::span<const canonical::FieldSpec> island_fields()noexcept;
Status capture_island(const SpIslandView &,canonical::Identity,const IslandMappings &,std::span<canonical::Identity> scratch,IslandImage &)noexcept;
// Writes only the caller's private preallocated candidate staging. The staging
// contents after failure are not publishable; callers discard/overwrite them.
Status restore_island(const canonical::Record &,const IslandMappings &,std::span<canonical::Identity> scratch,SpIslandView &)noexcept;
}
