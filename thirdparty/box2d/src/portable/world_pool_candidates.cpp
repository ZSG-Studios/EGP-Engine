// SPDX-License-Identifier: MIT
#include "world_pool_candidates.hpp"
#include <utility>
namespace superpos::box2d_portable {
WorldPoolCandidates::WorldPoolCandidates(WorldPoolCandidates&&other)noexcept{for(size_t i=0;i<pools_.size();++i)if(other.pools_[i]){pools_[i].emplace(std::move(*other.pools_[i]));other.pools_[i].reset();}}
size_t WorldPoolCandidates::reserved_payload_bytes()const noexcept{size_t result=0;for(const auto&p:pools_)if(p)result+=p->backing_bytes();return result;}
Result<WorldPoolCandidates> restore_world_pool_candidates(const std::array<PoolCandidateInput,SP_POOL_COUNT>&input,Allocator&allocator)noexcept{
 return restore_world_pool_candidates_placed(input,{},allocator);
}
Result<WorldPoolCandidates> restore_world_pool_candidates_placed(const std::array<PoolCandidateInput,SP_POOL_COUNT>&input,const std::array<std::span<const uint32_t>,SP_POOL_COUNT>&placement,Allocator&allocator)noexcept{
 size_t total=0;for(const auto&p:input){if(!p.pool_record||p.occupants.size()>100000||total>700000-p.occupants.size())return fail(Error::InvalidArgument);total+=p.occupants.size();}
 WorldPoolCandidates staged;
 for(uint32_t p=0;p<SP_POOL_COUNT;++p){auto candidate=restore_pool_occupants_placed(SpWorldPool(p),input[p].occupants,*input[p].pool_record,placement[p],allocator);if(!candidate)return fail(candidate.error());staged.pools_[p].emplace(std::move(*candidate));}
 return staged;
}
}
