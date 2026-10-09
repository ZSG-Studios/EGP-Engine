// SPDX-License-Identifier: MIT
#pragma once
#include "solver_set_codec.hpp"
#include "contact_codec.hpp"
#include "dynamic_tree_codec.hpp"
#include "solver_payload_bridge.h"
namespace superpos::box2d_portable {
struct SolverPayloadMappings {
 SolverMembershipMaps membership;
 // Per-record prepared-index maps may differ after sleeping-set transfers.
 std::span<const JointSimMappings> joints;
 std::span<const ContactMappings> contacts;
};
struct SolverPayloadScratch {
 // Body, joint, contact, island ID arrays. Restore requires full native capacities.
 std::array<std::span<int>,4> ids;
 std::span<canonical::Identity> identities;
};
struct SolverPayloadImages {
 std::span<BodySimImage> bodies;std::span<BodyStateImage> states;
 std::span<JointSimImage> joints;std::span<ContactSimImage> contacts;std::span<IslandSimImage> islands;
};
struct SolverPayloadRecords {
 std::span<const canonical::Record> bodies,states,joints,contacts,islands;
 std::span<const canonical::Record *const> joint_payloads;
};
Status capture_solver_payload(const SpSolverPayload &,canonical::Identity,const SolverPayloadMappings &,TreeCaptureBoundary,SolverPayloadScratch,SolverMembershipImage &,SolverPayloadImages)noexcept;
// Writes only a private candidate. Failure can leave candidate bytes modified;
// callers discard it and must not publish or pass it to engine callbacks.
Status restore_solver_payload(const canonical::Record &,SolverPayloadRecords,const SolverPayloadMappings &,TreeCaptureBoundary,SolverPayloadScratch,SpSolverPayload &)noexcept;
}
