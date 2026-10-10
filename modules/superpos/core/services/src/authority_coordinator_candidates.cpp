// SPDX-License-Identifier: MIT
#include "superpos/service/coordinator.hpp"
#include <algorithm>

namespace superpos::service {
namespace {
constexpr std::uint8_t known_transports=candidate_transport_udp|candidate_transport_webrtc;
}
Result<WarmCandidateSet> WarmCandidateSet::create(std::uint32_t capacity,std::uint32_t lag) noexcept {
    if(!capacity||capacity>maximum_warm_candidates||!lag||lag>(1u<<24))return fail(Error::InvalidArgument);
    WarmCandidateSet result;result.capacity_=capacity;result.lag_=lag;return result;
}
WarmCandidateSet::Entry* WarmCandidateSet::find(PeerId peer) noexcept {
    for(std::uint32_t i=0;i<count_;++i)if(entries_[i].claim.peer==peer)return &entries_[i];return nullptr;
}
const WarmCandidateSet::Entry* WarmCandidateSet::find(PeerId peer) const noexcept {
    for(std::uint32_t i=0;i<count_;++i)if(entries_[i].claim.peer==peer)return &entries_[i];return nullptr;
}
Status WarmCandidateSet::membership(std::uint64_t generation,std::span<const CandidateMember> members) noexcept {
    if(!generation||members.empty()||members.size()>maximum_candidate_members)return fail(Error::InvalidArgument);
    if(membership_known_&&generation<generation_)return fail(Error::StaleGeneration);
    for(std::size_t i=0;i<members.size();++i){
        if(!members[i].member||!members[i].transports||(members[i].transports&~known_transports))return fail(Error::InvalidArgument);
        for(std::size_t j=0;j<i;++j)if(members[j].member==members[i].member)return fail(Error::InvalidArgument);
    }
    if(membership_known_&&generation==generation_){
        // The same generation must describe the same membership.
        if(members.size()!=member_count_||!std::equal(members.begin(),members.end(),members_.begin()))return fail(Error::ProtocolViolation);
        return {};
    }
    std::copy(members.begin(),members.end(),members_.begin());member_count_=static_cast<std::uint32_t>(members.size());
    generation_=generation;membership_known_=true;
    for(std::uint32_t i=0;i<count_;++i)if(entries_[i].claim.membership_generation!=generation)entries_[i].state=CandidateState::Stale;
    return {};
}
Status WarmCandidateSet::requirements(const AuthorityRecoveryRequirements& value) noexcept {
    if(!known_fingerprint(value.schemas)||!known_fingerprint(value.simulation)||!known_fingerprint(value.codec)||
        !value.complete_world_bytes||value.restore_staging_bytes>UINT64_MAX-value.complete_world_bytes)return fail(Error::InvalidArgument);
    requirements_=value;
    // A stricter profile or a larger world invalidates earlier admissions.
    for(std::uint32_t i=0;i<count_;++i)if(entries_[i].state==CandidateState::Ready){
        auto claim=entries_[i].claim;claim.paths={entries_[i].paths.data(),entries_[i].path_count};
        if(!eligible(claim))entries_[i].state=CandidateState::Stale;
    }
    return {};
}
Status WarmCandidateSet::eligible(const WarmCandidateClaim& c) const noexcept {
    if(!c.peer||static_cast<unsigned>(c.platform)>2||c.paths.size()>maximum_candidate_members)return fail(Error::InvalidArgument);
    if(!membership_known_||!requirements_)return fail(Error::NotReady);
    if(c.membership_generation!=generation_)return fail(Error::StaleGeneration);
    if(!c.hidden_state_authorized)return fail(Error::PermissionDenied);
    if(!c.foreground)return fail(Error::NotReady);
    const auto& r=*requirements_;
    if(c.schemas!=r.schemas||c.simulation!=r.simulation||c.codec!=r.codec)return fail(Error::IncompatibleSchema);
    if(c.reserved_capacity_bytes<r.complete_world_bytes+r.restore_staging_bytes)return fail(Error::CapacityExceeded);
    for(std::size_t i=0;i<c.paths.size();++i){
        const auto t=c.paths[i].transports;
        if(!c.paths[i].member||(t&~known_transports))return fail(Error::InvalidArgument);
        // Browsers have no native UDP; a claim saying otherwise is malformed.
        if(c.platform==CandidatePlatform::Browser&&(t&candidate_transport_udp))return fail(Error::ProtocolViolation);
        for(std::size_t j=0;j<i;++j)if(c.paths[j].member==c.paths[i].member)return fail(Error::InvalidArgument);
    }
    // Every current member needs one shared transport family with the
    // candidate, so a browser cannot inherit UDP-only members.
    for(std::uint32_t m=0;m<member_count_;++m){
        const auto& member=members_[m];if(member.member==c.peer)continue;
        auto path=std::find_if(c.paths.begin(),c.paths.end(),[&](const CandidateMember& p){return p.member==member.member;});
        if(path==c.paths.end()||!(path->transports&member.transports))return fail(Error::Unsupported);
    }
    return {};
}
Status WarmCandidateSet::admit(const WarmCandidateClaim& claim) noexcept {
    if(auto r=eligible(claim);!r)return r;
    auto* entry=find(claim.peer);
    if(!entry){if(count_==capacity_)return fail(Error::CapacityExceeded);entry=&entries_[count_++];}
    entry->claim=claim;entry->claim.paths={};
    std::copy(claim.paths.begin(),claim.paths.end(),entry->paths.begin());entry->path_count=static_cast<std::uint32_t>(claim.paths.size());
    entry->state=CandidateState::Ready;return {};
}
Status WarmCandidateSet::suspend(PeerId peer) noexcept {
    auto* entry=find(peer);if(!entry)return fail(Error::NotReady);
    if(entry->claim.platform==CandidatePlatform::Native)return fail(Error::InvalidArgument);
    entry->state=CandidateState::Suspended;entry->claim.foreground=false;return {};
}
Status WarmCandidateSet::remove(PeerId peer) noexcept {
    auto* entry=find(peer);if(!entry)return fail(Error::NotReady);
    const auto index=static_cast<std::uint32_t>(entry-entries_.data());
    for(std::uint32_t i=index+1;i<count_;++i)entries_[i-1]=entries_[i];
    entries_[--count_]=Entry{};return {};
}
Status WarmCandidateSet::progress(PeerId peer,Tick replicated,Tick recoverable) noexcept {
    auto* entry=find(peer);if(!entry)return fail(Error::NotReady);
    if(replicated<entry->claim.replicated_tick)return fail(Error::StaleGeneration);
    entry->claim.replicated_tick=replicated;
    const bool lagging=recoverable>replicated&&recoverable-replicated>lag_;
    if(lagging&&entry->state==CandidateState::Ready)entry->state=CandidateState::Lagging;
    else if(!lagging&&entry->state==CandidateState::Lagging)entry->state=CandidateState::Ready;
    return {};
}
bool WarmCandidateSet::ready(PeerId peer,std::uint64_t generation) const noexcept {
    const auto* entry=find(peer);
    return entry&&entry->state==CandidateState::Ready&&membership_known_&&generation==generation_&&entry->claim.membership_generation==generation;
}
Result<WarmCandidateStatus> WarmCandidateSet::status(PeerId peer) const noexcept {
    const auto* entry=find(peer);if(!entry)return fail(Error::NotReady);
    return WarmCandidateStatus{entry->claim.peer,entry->claim.platform,entry->state,entry->claim.membership_generation,entry->claim.replicated_tick};
}
Result<WarmCandidateStatus> WarmCandidateSet::best() const noexcept {
    const Entry* chosen=nullptr;
    for(std::uint32_t i=0;i<count_;++i){
        const auto& e=entries_[i];if(e.state!=CandidateState::Ready)continue;
        if(!chosen||e.claim.replicated_tick>chosen->claim.replicated_tick||
            (e.claim.replicated_tick==chosen->claim.replicated_tick&&e.claim.peer<chosen->claim.peer))chosen=&e;
    }
    if(!chosen)return fail(Error::RecoveryUnavailable);
    return status(chosen->claim.peer);
}
}
