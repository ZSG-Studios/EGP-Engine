// SPDX-License-Identifier: MIT
#pragma once
#include "island_codec.hpp"
#include "joint_sim_codec.hpp"
#include "solver_set_bridge.h"
namespace superpos::box2d_portable {
constexpr uint32_t island_sim_kind=0x1403;
struct IslandSimImage {canonical::Atom atom{};canonical::Field field{};canonical::Record record{};IslandSimImage()noexcept;IslandSimImage(const IslandSimImage &)=delete;IslandSimImage &operator=(const IslandSimImage &)=delete;};
std::span<const canonical::FieldSpec> island_sim_fields()noexcept;
Status capture_island_sim(int,canonical::Identity,const canonical::IdentityMap &,IslandSimImage &)noexcept;
Result<int> restore_island_sim(const canonical::Record &,const canonical::IdentityMap &)noexcept;
struct SolverMembershipMaps {const canonical::IdentityMap &set,&body,&joint,&contact,&island;};
struct SolverMembershipImage {
 // One column for each native array, in native order. Buffers are caller-owned.
 std::array<std::span<canonical::Atom>,5> storage;
 std::array<canonical::Atom,7> scalars{};
 std::array<canonical::Field,12> fields{};canonical::Record record{};
 explicit SolverMembershipImage(std::array<std::span<canonical::Atom>,5>)noexcept;
 SolverMembershipImage(const SolverMembershipImage &)=delete;SolverMembershipImage &operator=(const SolverMembershipImage &)=delete;
};
std::span<const canonical::FieldSpec> solver_membership_fields()noexcept;
Status capture_solver_membership(const SpSolverMembership &,canonical::Identity,const SolverMembershipMaps &,std::span<canonical::Identity> scratch,SolverMembershipImage &)noexcept;
Status restore_solver_membership(const canonical::Record &,const SolverMembershipMaps &,std::span<canonical::Identity> scratch,SpSolverMembership &)noexcept;
}
