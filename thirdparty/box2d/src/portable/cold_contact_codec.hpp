// SPDX-License-Identifier: MIT
#pragma once
#include "contact_codec.hpp"
#include "cold_contact_bridge.h"
namespace superpos::box2d_portable {
constexpr std::uint32_t solver_set_kind=0x1401,island_kind=0x1402;
struct ColdContactMappings {const canonical::IdentityMap &contact,&shape,&body,&island,&island_member,&solver_set,&solver_member;};
struct ColdContactImage {
 std::array<canonical::Atom,21> atoms{};std::array<canonical::Field,21> fields{};canonical::Record record{};
 ColdContactImage() noexcept;
 ColdContactImage(const ColdContactImage &)=delete;ColdContactImage &operator=(const ColdContactImage &)=delete;
};
std::span<const canonical::FieldSpec> cold_contact_fields() noexcept;
Status capture_cold_contact(const SpColdContact &,canonical::Identity,const ColdContactMappings &,ColdContactImage &) noexcept;
Status restore_cold_contact(const canonical::Record &,const ColdContactMappings &,SpColdContact &) noexcept;
}
