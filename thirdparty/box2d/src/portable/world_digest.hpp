// SPDX-License-Identifier: MIT
#pragma once
#include "world_events.hpp"
#include "sha256.hpp"
#include "world_digest_bridge.h"
namespace superpos::box2d_portable {
// Maps that name every native slot of one world (source or candidate) by its
// canonical identity, plus the admitted identities of singleton records.
struct WorldDigestMaps {
 std::array<const canonical::IdentityMap*,SP_POOL_COUNT> objects{},slots{};
 std::array<const canonical::IdentityMap*,3> nodes{},proxies{};
 const canonical::IdentityMap*sensors{},*move_events{};
 const canonical::VersionedIdentityMap*body_history{},*shape_history{},*contact_history{},*joint_history{};
 const canonical::PointerIdentityMap*bindings{};uint16_t world0{};
 canonical::Identity root{},graph{},broadphase{};std::array<canonical::Identity,24> colors{};BroadPhaseIds broadphase_ids{};std::array<canonical::Identity,9> events{};
};
using WorldDigest=std::array<std::byte,32>;
// SHA-256 over a canonical traversal of a complete native world: root
// scalars; the seven pools (free order and per-slot occupant and generation
// by slot identity); cold bodies, contacts and joints; every solver set's
// membership, body simulations, awake states and non-graph constraints; islands;
// the 24 graph colors with their constraints and the graph root; shapes and
// geometry; chains; sensors; tree nodes and trees; moves; pairs; the broadphase
// root; and all nine event arrays. Each component kind is ordered by canonical
// identity, so the digest is independent of native slot layout.
// Retained prepared solver indices of non-graph constraints are normalized to
// the null index before capture. Normalized runtime scratch, world identity,
// lifecycle flags and execution-environment bindings are excluded. The world
// must satisfy the runtime scratch capture barrier.
Result<WorldDigest> digest_world(const void*world,const WorldDigestMaps&,Allocator&)noexcept;
// The traversal behind digest_world, exposed for checkpoint capture: every
// canonical record is passed to the visitor in digest order with its
// component tag (index = graph color, tree or event array where relevant).
enum class RecordTag:uint8_t{Root,ColdBody,ColdContact,ColdJoint,SetMembership,BodySim,BodyState,SetContact,SetJoint,SetJointPayload,Island,GraphColor,GraphContact,GraphJoint,GraphJointPayload,GraphRoot,ShapeBase,ShapeGeometry,Chain,Sensor,TreeNode,Tree,Moves,Pairs,Broadphase,Event};
class WorldRecordVisitor{public:virtual ~WorldRecordVisitor()=default;
 virtual void record(RecordTag,uint32_t index,const canonical::Record&)noexcept=0;
 virtual void pool_header(uint32_t,uint32_t,uint32_t)noexcept{}
 virtual void pool_free(uint32_t,canonical::Identity)noexcept{}
 virtual void pool_slot(uint32_t,canonical::Identity,canonical::Identity,uint32_t)noexcept{}};
Status visit_world(const void*world,const WorldDigestMaps&,Allocator&,WorldRecordVisitor&)noexcept;
// Diagnostic only: ordinal of the last failing traversal check (0 = none).
uint32_t last_visit_failure()noexcept;
}
