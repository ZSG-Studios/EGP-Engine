// SPDX-License-Identifier: MIT
#pragma once
#include "cold_body_codec.hpp"
#include "cold_joint_bridge.h"
namespace superpos::box2d_portable {
struct ColdJointMappings {const canonical::IdentityMap &joint,&body,&solver_set,&solver_member,&island,&island_member;const canonical::PointerIdentityMap &binding;};
struct ColdJointImage {std::array<canonical::Atom,22> atoms{};std::array<canonical::Field,22> fields{};canonical::Record record{};
 ColdJointImage()noexcept;ColdJointImage(const ColdJointImage &)=delete;ColdJointImage &operator=(const ColdJointImage &)=delete;};
std::span<const canonical::FieldSpec> cold_joint_fields()noexcept;
Status capture_cold_joint(const SpColdJoint &,canonical::Identity,const ColdJointMappings &,ColdJointImage &)noexcept;
Status restore_cold_joint(const canonical::Record &,const ColdJointMappings &,SpColdJoint &)noexcept;
}
