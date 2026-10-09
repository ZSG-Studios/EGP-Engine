// SPDX-License-Identifier: MIT
#pragma once
#include "shape_codec.hpp"
namespace superpos::box2d_portable {
constexpr uint32_t tree_node_kind=0x1D00;
// All capacity slots, including free nodes, require distinct registered tokens.
struct TreeNodeImage {
 std::array<canonical::Atom,13> atoms{};
 std::array<canonical::Field,13> fields{};
 canonical::Record record{};
 TreeNodeImage()noexcept;
 TreeNodeImage(const TreeNodeImage &)=delete;
 TreeNodeImage &operator=(const TreeNodeImage &)=delete;
};
std::span<const canonical::FieldSpec> tree_node_fields()noexcept;
Status capture_tree_node(const b2TreeNode &,canonical::Identity,const canonical::IdentityMap &nodes,const canonical::IdentityMap &shapes,TreeNodeImage &)noexcept;
Status restore_tree_node(const canonical::Record &,const canonical::IdentityMap &nodes,const canonical::IdentityMap &shapes,b2TreeNode &)noexcept;
}
