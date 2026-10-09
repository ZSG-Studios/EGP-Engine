// SPDX-License-Identifier: MIT
#pragma once
#include "owned_constraint_graph.hpp"
#include "inactive_constraints.hpp"
#include "cold_contact_codec.hpp"
#include "cold_joint_codec.hpp"
namespace superpos::box2d_portable {
struct OwnedConstraintRecords {
 // Complete cold contact/joint records for every live constraint, plus the
 // non-graph contact/joint simulation records and their tagged joint payloads.
 std::span<const canonical::Record> cold_contacts,cold_joints,contacts,joints,joint_payloads;
};
// Composes the owned awake graph with every solver set's non-graph constraint
// arrays. Constraint endpoints derive from the complete cold contact/joint
// records, whose set/color/local and island membership must agree with the
// owned placement. Cold edge lists must be reciprocal. Cold records are
// validated here but not yet owned as native b2Contact/b2Joint arrays; no
// native solver set, world root, geometry, event or engine adoption exists.
class OwnedSolverConstraints {
 Allocator*allocator_{};void*block_{};size_t bytes_{};
 std::optional<OwnedConstraintGraph> prior_;
 std::span<std::optional<OwnedInactiveConstraints>> sets_;
 std::span<ConstraintEndpoints> contact_ends_,joint_ends_;
 std::span<SpColdContact> cold_contacts_;std::span<SpColdJoint> cold_joints_;
 OwnedSolverConstraints()noexcept=default;
 friend Result<OwnedSolverConstraints>restore_owned_solver_constraints(OwnedConstraintGraph&&,OwnedConstraintRecords,Allocator&,const canonical::PointerIdentityMap*)noexcept;
public:
 OwnedSolverConstraints(const OwnedSolverConstraints&)=delete;OwnedSolverConstraints&operator=(const OwnedSolverConstraints&)=delete;
 OwnedSolverConstraints(OwnedSolverConstraints&&)noexcept;OwnedSolverConstraints&operator=(OwnedSolverConstraints&&)=delete;~OwnedSolverConstraints();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedConstraintGraph&graph()const noexcept{return *prior_;}
 // Indexed by destination solver-set slot; empty for free set slots.
 std::span<const std::optional<OwnedInactiveConstraints>>sets()const noexcept{return sets_;}
 // Indexed by destination native contact/joint ID; free slots hold -1.
 std::span<const ConstraintEndpoints>contact_endpoints()const noexcept{return contact_ends_;}
 std::span<const ConstraintEndpoints>joint_endpoints()const noexcept{return joint_ends_;}
 // Validated destination cold records, indexed by native ID; entries whose
 // endpoint is -1 are free slots and carry no record.
 std::span<const SpColdContact>cold_contacts()const noexcept{return cold_contacts_;}
 std::span<const SpColdJoint>cold_joints()const noexcept{return cold_joints_;}
 size_t reserved_payload_bytes()const noexcept;
};
// The prior graph owner is moved only on success; failure preserves it and
// returns every new charge.
Result<OwnedSolverConstraints>restore_owned_solver_constraints(OwnedConstraintGraph&&,OwnedConstraintRecords,Allocator&)noexcept;
// The same with an admitted binding map for cold-joint user data (null admits
// only null user data).
Result<OwnedSolverConstraints>restore_owned_solver_constraints(OwnedConstraintGraph&&,OwnedConstraintRecords,Allocator&,const canonical::PointerIdentityMap*bindings)noexcept;
}
