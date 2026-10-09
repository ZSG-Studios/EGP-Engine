// SPDX-License-Identifier: MIT
#pragma once
#include "contact_codec.hpp"
#include "joint_sim_codec.hpp"
#include "cold_body_codec.hpp"
#include "solver_set_bridge.h"
#include "inactive_constraints_bridge.h"
#include <superpos/allocator.hpp>
namespace superpos::box2d_portable {
// Destination native body IDs of a constraint's cold endpoints, indexed by the
// destination native constraint ID. Supplied by the complete-world owner.
struct ConstraintEndpoints {int body_a=-1,body_b=-1;};
struct InactiveConstraintRecords {std::span<const canonical::Record> contacts,joints,joint_payloads;};
struct InactiveConstraintMaps {const canonical::IdentityMap &contact,&joint,&body,&shape;};
// Owns one solver set's non-graph native b2ContactSim/b2JointSim arrays at
// their exact recorded capacities. Retained prepared solver indices are
// validated against cold endpoints and then normalized to the null index; see
// inactive_prepared_indices.md for the overwrite proof that permits this. This
// path never applies to active graph constraints. No native set, world
// adoption or complete-world capability is provided.
class OwnedInactiveConstraints {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*contacts_{},*joints_{};
 uint32_t contact_count_{},contact_capacity_{},joint_count_{},joint_capacity_{},contact_stride_{},joint_stride_{};int set_index_=-1;
 OwnedInactiveConstraints()noexcept=default;
 friend Result<OwnedInactiveConstraints>restore_inactive_constraints(const SpSolverMembership&,InactiveConstraintRecords,InactiveConstraintMaps,std::span<const ConstraintEndpoints>,std::span<const ConstraintEndpoints>,Allocator&)noexcept;
public:
 OwnedInactiveConstraints(const OwnedInactiveConstraints&)=delete;OwnedInactiveConstraints&operator=(const OwnedInactiveConstraints&)=delete;
 OwnedInactiveConstraints(OwnedInactiveConstraints&&)noexcept;OwnedInactiveConstraints&operator=(OwnedInactiveConstraints&&)=delete;~OwnedInactiveConstraints();
 int set_index()const noexcept{return set_index_;}
 uint32_t contact_count()const noexcept{return contact_count_;}
 uint32_t contact_capacity()const noexcept{return contact_capacity_;}
 uint32_t joint_count()const noexcept{return joint_count_;}
 uint32_t joint_capacity()const noexcept{return joint_capacity_;}
 const void*native_contact(uint32_t i)const noexcept{return i<contact_count_?static_cast<const std::byte*>(contacts_)+size_t(i)*contact_stride_:nullptr;}
 const void*native_joint(uint32_t i)const noexcept{return i<joint_count_?static_cast<const std::byte*>(joints_)+size_t(i)*joint_stride_:nullptr;}
 bool has_storage()const noexcept{return block_!=nullptr;}
 // Base of the full-capacity native arrays (nullptr only without storage).
 const void*native_contact_array()const noexcept{return contacts_;}
 const void*native_joint_array()const noexcept{return joints_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_;}
};
// Membership is the destination set's restored native order (static=0,
// disabled=1, awake=2, sleeping>=3). Awake arrays may hold only non-touching
// contacts; static arrays only joints. Every member must have exactly one
// canonical record and every supplied record and tagged payload must be used.
// On failure nothing is published and all charges are returned.
Result<OwnedInactiveConstraints>restore_inactive_constraints(const SpSolverMembership&,InactiveConstraintRecords,InactiveConstraintMaps,
 std::span<const ConstraintEndpoints> contact_endpoints,std::span<const ConstraintEndpoints> joint_endpoints,Allocator&)noexcept;
}
