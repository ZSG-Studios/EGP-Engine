// SPDX-License-Identifier: MIT
#pragma once
#include "joint_codec.hpp"
#include "joint_sim_bridge.h"
namespace superpos::box2d_portable {
constexpr std::uint32_t joint_sim_kind=0x1900;
struct JointSimMappings {const canonical::IdentityMap &joint,&body,&solver_body;};
struct JointSimImage {
 std::array<canonical::Atom,29> atoms{};std::array<canonical::Field,29> fields{};canonical::Record base{};std::array<canonical::Record,2> records{};size_t count{};
 DistanceJointImage distanceJoint;
 MotorJointImage motorJoint;
 PrismaticJointImage prismaticJoint;
 RevoluteJointImage revoluteJoint;
 WeldJointImage weldJoint;
 WheelJointImage wheelJoint;
 JointSimImage()noexcept;JointSimImage(const JointSimImage &)=delete;JointSimImage &operator=(const JointSimImage &)=delete;
};
std::span<const canonical::FieldSpec> joint_sim_fields()noexcept;
Status capture_joint_sim(const SpJointSim &,canonical::Identity,const JointSimMappings &,JointSimImage &)noexcept;
Status restore_joint_sim(const canonical::Record &,const canonical::Record *,const JointSimMappings &,SpJointSim &)noexcept;
}
