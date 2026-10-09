// SPDX-License-Identifier: MIT
#pragma once
#include "body_codec.hpp"
#include "contact_bridge.h"
namespace superpos::box2d_portable {
constexpr std::uint32_t contact_kind=0x1006,shape_kind=0x1007,contact_sim_kind=0x1300;
struct ContactMappings {const canonical::IdentityMap &contact,&shape,&body,&solver_body;};
struct ContactSimImage {
 std::array<canonical::Atom,59> atoms{};std::array<canonical::Field,59> fields{};canonical::Record record{};
 ContactSimImage() noexcept;
 ContactSimImage(const ContactSimImage &)=delete;ContactSimImage &operator=(const ContactSimImage &)=delete;
};
std::span<const canonical::FieldSpec> contact_sim_fields() noexcept;
Status capture_contact_sim(const SpContactSim &,canonical::Identity,const ContactMappings &,ContactSimImage &) noexcept;
Status restore_contact_sim(const canonical::Record &,const ContactMappings &,SpContactSim &) noexcept;
}
