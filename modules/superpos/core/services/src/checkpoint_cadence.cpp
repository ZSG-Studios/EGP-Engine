// SPDX-License-Identifier: MIT
#include "superpos/checkpoint_cadence.hpp"

namespace superpos {
namespace {
constexpr std::uint32_t maximum_cadence_ticks=1u<<24;
bool valid_ticket(const CheckpointTicket& t) noexcept {return t.match&&t.id&&t.generation;}
}
Result<CheckpointCadence> CheckpointCadence::create(CheckpointCadenceConfig config,Epoch epoch) noexcept {
    if(!epoch||!config.checkpoint_interval_ticks||config.checkpoint_interval_ticks>maximum_cadence_ticks||
        !config.maximum_publication_lag_ticks||config.maximum_publication_lag_ticks>maximum_cadence_ticks)return fail(Error::InvalidArgument);
    CheckpointCadence result;result.config_=config;result.state_.epoch=epoch;result.refresh();return result;
}
void CheckpointCadence::refresh() noexcept {
    state_.oldest=bases_[0];state_.newest=bases_[1]?bases_[1]:bases_[0];
    state_.recoverable.reset();
    if(committed_&&bases_[0]&&bases_[0]->sequence<=committed_->sequence)state_.recoverable=committed_;
    state_.overdue=state_.recoverable&&state_.newest&&state_.recoverable->tick>=state_.newest->tick&&
        state_.recoverable->tick-state_.newest->tick>2ULL*config_.checkpoint_interval_ticks;
}
Status CheckpointCadence::committed(const JournalPrefix& prefix) noexcept {
    if(prefix.epoch!=state_.epoch)return fail(Error::StaleEpoch);
    if(committed_&&(prefix.sequence<committed_->sequence||prefix.tick<committed_->tick||
        (prefix.sequence==committed_->sequence&&prefix.tick!=committed_->tick)))return fail(Error::StaleGeneration);
    committed_=prefix;refresh();return {};
}
Status CheckpointCadence::candidate(const CadenceCheckpoint& checkpoint) noexcept {
    if(!valid_ticket(checkpoint.ticket)||!checkpoint.sequence)return fail(Error::InvalidArgument);
    if(state_.candidate){if(*state_.candidate==checkpoint)return {};return fail(Error::Busy);}
    if(checkpoint.epoch!=state_.epoch)return fail(Error::StaleEpoch);
    if(!committed_||checkpoint.sequence!=committed_->sequence||checkpoint.tick!=committed_->tick)return fail(Error::StaleGeneration);
    for(const auto& base:bases_)if(base&&base->ticket==checkpoint.ticket)return fail(Error::ProtocolViolation);
    state_.candidate=checkpoint;++state_.captures;return {};
}
Status CheckpointCadence::abandoned(CheckpointTicket ticket) noexcept {
    if(!state_.candidate||state_.candidate->ticket!=ticket)return fail(Error::StaleGeneration);
    state_.candidate.reset();return {};
}
Status CheckpointCadence::verified(const CadenceCheckpoint& checkpoint) noexcept {
    if(!valid_ticket(checkpoint.ticket)||!checkpoint.sequence||!checkpoint.epoch)return fail(Error::InvalidArgument);
    if(state_.candidate){if(*state_.candidate!=checkpoint)return fail(Error::ProtocolViolation);}
    else if(checkpoint.epoch>state_.epoch||(committed_&&checkpoint.sequence>committed_->sequence))return fail(Error::ProtocolViolation);
    for(const auto& base:bases_)if(base&&base->ticket==checkpoint.ticket)return fail(Error::ProtocolViolation);
    if(bases_[1])return fail(Error::CapacityExceeded);
    if(!bases_[0])bases_[0]=checkpoint;
    else if(bases_[0]->sequence<=checkpoint.sequence)bases_[1]=checkpoint;
    else{bases_[1]=bases_[0];bases_[0]=checkpoint;}
    state_.candidate.reset();++state_.bases_installed;refresh();return {};
}
Status CheckpointCadence::replaced(CheckpointTicket candidate,CheckpointTicket retired) noexcept {
    if(!state_.candidate||state_.candidate->ticket!=candidate)return fail(Error::StaleGeneration);
    if(!bases_[1]||bases_[0]->ticket!=retired)return fail(Error::ProtocolViolation);
    if(state_.candidate->sequence<bases_[1]->sequence)return fail(Error::ProtocolViolation);
    bases_[0]=bases_[1];bases_[1]=state_.candidate;state_.candidate.reset();
    ++state_.replacements;state_.compaction_pending=true;refresh();return {};
}
Status CheckpointCadence::compacted(const JournalCompactionReceipt& receipt) noexcept {
    if(!bases_[0]||receipt.base!=bases_[0]->ticket)return fail(Error::StaleGeneration);
    if(receipt.complete)state_.compaction_pending=false;return {};
}
Result<bool> CheckpointCadence::publish(Tick tick) noexcept {
    if(state_.published&&tick<*state_.published)return fail(Error::InvalidArgument);
    const auto& r=state_.recoverable;
    const bool within=r&&(tick<=r->tick||tick-r->tick<=config_.maximum_publication_lag_ticks);
    if(!within){if(!state_.paused)++state_.pauses;state_.paused=true;return false;}
    state_.paused=false;
    const Tick lag=tick>r->tick?tick-r->tick:0;if(lag>state_.worst_lag)state_.worst_lag=lag;
    if(!state_.published||*state_.published!=tick)++state_.publications;
    state_.published=tick;return true;
}
CadenceAction CheckpointCadence::next() const noexcept {
    if(state_.candidate)return CadenceAction::Finish;
    if(state_.compaction_pending)return CadenceAction::Compact;
    if(!committed_||!committed_->sequence)return CadenceAction::None;
    const auto& newest=state_.newest;
    if(!newest)return CadenceAction::Capture;
    if(committed_->sequence>newest->sequence&&committed_->tick>=newest->tick&&
        committed_->tick-newest->tick>=config_.checkpoint_interval_ticks)return CadenceAction::Capture;
    return CadenceAction::None;
}
CadenceStatus CheckpointCadence::status() const noexcept {return state_;}
Result<CadencePromotion> CheckpointCadence::promote(Epoch successor,const JournalPrefix& restored) noexcept {
    if(successor<=state_.epoch)return fail(Error::StaleEpoch);
    if(committed_&&(restored.sequence<committed_->sequence||restored.tick<committed_->tick))return fail(Error::ProtocolViolation);
    // No weaker fallback: a successor needs a verified base at or before C.
    if(!bases_[0]||bases_[0]->sequence>restored.sequence)return fail(Error::RecoveryUnavailable);
    CadencePromotion report{state_.epoch,successor,state_.published,restored,0};
    report.recoverable.epoch=successor;
    if(state_.published&&*state_.published>restored.tick)report.rolled_back_ticks=*state_.published-restored.tick;
    state_.epoch=successor;committed_=report.recoverable;state_.published=restored.tick;
    state_.paused=false;state_.candidate.reset();refresh();return report;
}
}
