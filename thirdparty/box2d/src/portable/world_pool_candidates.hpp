// SPDX-License-Identifier: MIT
#pragma once
#include "pool_occupants.hpp"
namespace superpos::box2d_portable {
struct PoolCandidateInput {
 std::span<const canonical::Record> occupants;
 const canonical::Record*pool_record{};
};
// Owned allocation/lifetime tables for all seven world pools. Native object,
// solver, geometry, event and application payloads are not yet contained here.
// No engine publication operation is provided until those participants exist.
class WorldPoolCandidates {
 std::array<std::optional<OwnedPoolCandidate>,SP_POOL_COUNT> pools_;
 WorldPoolCandidates()noexcept=default;
 friend Result<WorldPoolCandidates> restore_world_pool_candidates(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,Allocator&)noexcept;
 friend Result<WorldPoolCandidates> restore_world_pool_candidates_placed(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,const std::array<std::span<const uint32_t>,SP_POOL_COUNT>&,Allocator&)noexcept;
public:
 WorldPoolCandidates(const WorldPoolCandidates&)=delete;
 WorldPoolCandidates&operator=(const WorldPoolCandidates&)=delete;
 WorldPoolCandidates(WorldPoolCandidates&&)noexcept;
 WorldPoolCandidates&operator=(WorldPoolCandidates&&)=delete;
 const OwnedPoolCandidate*pool(SpWorldPool p)const noexcept{return p>=SP_POOL_BODY&&p<SP_POOL_COUNT&&pools_[p]?&*pools_[p]:nullptr;}
 size_t reserved_payload_bytes()const noexcept;
};
// Failure at any pool destroys every earlier candidate. Success only transfers
// ownership; no allocation, callback, or engine mutation occurs in that move.
Result<WorldPoolCandidates> restore_world_pool_candidates(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,Allocator&)noexcept;
Result<WorldPoolCandidates> restore_world_pool_candidates_placed(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,const std::array<std::span<const uint32_t>,SP_POOL_COUNT>&,Allocator&)noexcept;
}
