// SPDX-License-Identifier: MIT
#pragma once
#include "graph_color_codec.hpp"
namespace superpos::box2d_portable {
constexpr uint32_t constraint_graph_kind=0x2101;
struct ConstraintGraphScratch {
 std::span<uint32_t> color, identities;
 std::array<std::span<canonical::Atom>,3> color_atoms;
};
struct ConstraintGraphImage {
 std::array<canonical::Atom,24> atoms{};
 canonical::Field field{};canonical::Record record{};
 ConstraintGraphImage()noexcept;
 ConstraintGraphImage(const ConstraintGraphImage &)=delete;ConstraintGraphImage &operator=(const ConstraintGraphImage &)=delete;
};
std::span<const canonical::FieldSpec> constraint_graph_fields()noexcept;
Status capture_constraint_graph(std::span<const SpGraphColorView>,std::span<const canonical::Identity>,canonical::Identity,const GraphColorMaps &,TreeCaptureBoundary,ConstraintGraphScratch,ConstraintGraphImage &)noexcept;
Status validate_restored_constraint_graph(const canonical::Record &,std::span<const canonical::Record>,std::span<const SpGraphColorView>,const GraphColorMaps &,TreeCaptureBoundary,ConstraintGraphScratch)noexcept;
}
