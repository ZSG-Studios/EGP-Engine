// SPDX-License-Identifier: MIT
#pragma once
#include "tree_topology.hpp"
namespace superpos::box2d_portable {
constexpr uint32_t dynamic_tree_kind=0x1D01;
// Private caller assertion only; the engine capture barrier is not yet integrated.
// This profile follows native dynamic_tree.c B2_TREE_HEURISTIC == 0.
struct TreeCaptureBoundary { bool physics_jobs_drained{}, rebuild_drained{}; uint32_t heuristic{}; };
struct DynamicTreeImage {
 std::array<canonical::Atom,6> atoms{};
 std::span<canonical::Atom> node_storage;
 std::array<canonical::Field,7> fields{};canonical::Record record{};
 explicit DynamicTreeImage(std::span<canonical::Atom>)noexcept;
 DynamicTreeImage(const DynamicTreeImage &)=delete;DynamicTreeImage &operator=(const DynamicTreeImage &)=delete;
};
struct DynamicTreeStorage {
 std::span<b2TreeNode> nodes;
 std::span<int32_t> leaf_indices;
 std::span<b2Vec2> leaf_centers;
};
std::span<const canonical::FieldSpec> dynamic_tree_fields()noexcept;
Status capture_dynamic_tree(const b2DynamicTree &,canonical::Identity,const canonical::IdentityMap &,TreeCaptureBoundary,std::span<uint8_t> marks,std::span<uint32_t> stack,DynamicTreeImage &)noexcept;
// Outputs reference the caller's unpublished storage. Ownership is never transferred.
Result<b2DynamicTree> restore_dynamic_tree(const canonical::Record &,std::span<const canonical::Record> node_records,const canonical::IdentityMap &nodes,const canonical::IdentityMap &shapes,TreeCaptureBoundary,DynamicTreeStorage,std::span<uint8_t> marks,std::span<uint32_t> stack)noexcept;
}
