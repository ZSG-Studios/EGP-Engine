// SPDX-License-Identifier: MIT
#pragma once
#include "contact_codec.hpp"
#include "visitor_bridge.h"
#include <superpos/canonical_versioned_map.hpp>
namespace superpos::box2d_portable {
constexpr std::uint32_t visitor_kind=0x1601;
struct VisitorImage {canonical::Atom atom{};canonical::Field field{};canonical::Record record{};VisitorImage()noexcept;VisitorImage(const VisitorImage &)=delete;VisitorImage &operator=(const VisitorImage &)=delete;};
std::span<const canonical::FieldSpec> visitor_fields()noexcept;
Status capture_visitor(const SpVisitor &,canonical::Identity,const canonical::VersionedIdentityMap &,VisitorImage &)noexcept;
Status restore_visitor(const canonical::Record &,const canonical::VersionedIdentityMap &,SpVisitor &)noexcept;
}
