// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/canonical_checkpoint.hpp>
#include <array>
#include <box2d/collision.h>
namespace superpos::box2d_portable {
// Geometric feature IDs remain explicit uint16 values bound to canonical
// geometry identity, not native shape slots. Contact routing uses simulation ID.
constexpr std::uint32_t manifold_kind=0x1003;
struct ManifoldImage {
    std::array<canonical::Atom,28> atoms{};
    std::array<canonical::Field,13> fields{};
    canonical::Record record{};
    ManifoldImage() noexcept;
    ManifoldImage(const ManifoldImage &)=delete;
    ManifoldImage &operator=(const ManifoldImage &)=delete;
};
std::span<const canonical::FieldSpec> manifold_fields() noexcept;
Status capture_manifold(const b2Manifold &,canonical::Identity,ManifoldImage &) noexcept;
// Validates every field and stages a complete native manifold before the one
// callback-free/no-allocation assignment. Failure never mutates destination.
Status restore_manifold(const canonical::Record &,b2Manifold &) noexcept;
}
