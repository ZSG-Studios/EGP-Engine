// SPDX-License-Identifier: MIT
#pragma once
#include "cold_contact_codec.hpp"
#include <superpos/canonical_pointer_map.hpp>
namespace superpos::box2d_portable {
constexpr std::uint32_t joint_kind=0x1008,chain_kind=0x1009,body_move_kind=0x1701,binding_kind=0x1801;
struct ColdBodyMappings {const canonical::IdentityMap &body,&solver_set,&solver_member,&shape,&chain,&island,&island_member,&move_event,&contact,&joint;const canonical::PointerIdentityMap &binding;};
struct ColdBodyImage {std::array<canonical::Atom,33> atoms{};std::array<canonical::Field,24> fields{};canonical::Record record{};
 ColdBodyImage()noexcept;ColdBodyImage(const ColdBodyImage &)=delete;ColdBodyImage &operator=(const ColdBodyImage &)=delete;};
std::span<const canonical::FieldSpec> cold_body_fields()noexcept;
Status capture_cold_body(const b2Body &,canonical::Identity,const ColdBodyMappings &,ColdBodyImage &)noexcept;
Status restore_cold_body(const canonical::Record &,const ColdBodyMappings &,b2Body &)noexcept;
}
