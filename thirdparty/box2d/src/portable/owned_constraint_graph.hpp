// SPDX-License-Identifier: MIT
#pragma once
#include "owned_islands.hpp"
#include "constraint_graph_codec.hpp"
#include "cold_body_codec.hpp"
#include "owned_graph_bridge.h"
namespace superpos::box2d_portable {
struct OwnedGraphRecords {
 const canonical::Record*root{};
 std::span<const canonical::Record>colors,cold_bodies,contacts,joints,joint_payloads;
};
// Actual native awake graph ownership only. Sleeping/disabled set constraints,
// complete cold objects, geometry, events and engine adoption remain separate.
class OwnedConstraintGraph {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*graph_{};
 std::optional<OwnedIslands> prior_;
 OwnedConstraintGraph()noexcept=default;
 friend Result<OwnedConstraintGraph>restore_owned_constraint_graph(OwnedIslands&&,OwnedGraphRecords,Allocator&)noexcept;
public:
 OwnedConstraintGraph(const OwnedConstraintGraph&)=delete;OwnedConstraintGraph&operator=(const OwnedConstraintGraph&)=delete;
 OwnedConstraintGraph(OwnedConstraintGraph&&)noexcept;OwnedConstraintGraph&operator=(OwnedConstraintGraph&&)=delete;~OwnedConstraintGraph();
 const void*native_graph()const noexcept{return graph_;}
 const OwnedIslands&islands()const noexcept{return *prior_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
// Prior owner is moved only after successful validation; failure preserves it.
// Cold body records establish exact types, stable flags, generations and set
// membership. Other cold fields still require full-world participant validation.
Result<OwnedConstraintGraph>restore_owned_constraint_graph(OwnedIslands&&,OwnedGraphRecords,Allocator&)noexcept;
// Diagnostic only: ordinal of the last failing check in this module (0 = none).
extern uint32_t owned_graph_failure;
}
