// SPDX-License-Identifier: MIT
// Included in the journal ordering domain after checkpoint storage/service.
// Restore evidence is derived from this store only; scopes are compared, never
// trusted as a source of checkpoint, prefix or digest values.
namespace {
bool same_restore_authority(const DurableAuthorityReceipt& stored,const DurableAuthorityReceipt& expected) noexcept {
    const auto& a=stored.fenced_prefix;const auto& b=expected.fenced_prefix;
    return same_authority_proposal(stored.proposal,expected.proposal)&&stored.boot==expected.boot&&
        a.epoch==b.epoch&&a.sequence==b.sequence&&a.tick==b.tick&&a.retained_bytes==b.retained_bytes;
}
bool same_profile(const CanonicalStateHeader& a,const CanonicalStateHeader& b) noexcept {
    return a.schemas==b.schemas&&a.simulation==b.simulation&&a.participants==b.participants&&a.codec==b.codec;
}
// The anchor of one stored image, derived from storage only. Its hash-checked
// first content chunk must begin exactly one canonical checkpoint envelope that
// matches the manifest position and profile, and the committed journal record
// at that sequence must report the same profile and complete-state digest.
template<class Store> Result<CanonicalStateHeader> bound_checkpoint_anchor(Store& s,const CheckpointContentReceipt& image) noexcept {
    const auto& m=image.manifest;
    std::array<std::byte,checkpoint_chunk_bytes> first{};Result<std::size_t> read=fail(Error::NotReady);
    {CheckpointCallbackGuard guard(s.checkpoint_callback);read=checkpoint_read_data(s,image,0,first);}
    if(!read)return fail(read.error());
    auto anchor=decode_canonical_state_header(std::span(first).first(*read),m.bytes);if(!anchor)return fail(Error::RecoveryUnavailable);
    if(anchor->kind!=CanonicalStateKind::Checkpoint||anchor->position!=CanonicalStatePosition{m.match,m.epoch,m.sequence,m.tick}||
        anchor->schemas!=m.schemas||anchor->simulation!=m.simulation||anchor->participants!=m.participants)return fail(Error::RecoveryUnavailable);
    Statement at;if(auto r=at.prepare(s.database,"SELECT epoch,tick,data FROM records WHERE match=?1 AND seq=?2");!r)return fail(r.error());
    if(auto r=at.number(1,s.config.match);!r)return fail(r.error());if(auto r=at.number(2,m.sequence);!r)return fail(r.error());
    const auto code=sqlite3_step(at.value);if(code!=SQLITE_ROW)return fail(code==SQLITE_DONE?Error::RecoveryUnavailable:sql_error(code));
    auto epoch=at.number(0),tick=at.number(1);if(!epoch||!tick||*tick!=m.tick||*epoch>m.epoch)return fail(Error::RecoveryUnavailable);
    auto bytes=sqlite_blob(at.value,2,0,s.config.maximum_record_bytes);if(!bytes)return fail(bytes.error());
    auto record=decode_canonical_state(*bytes);
    if(!record||record->header.position!=CanonicalStatePosition{s.config.match,*epoch,m.sequence,*tick}||
        (record->header.kind!=CanonicalStateKind::FullRecord&&record->header.kind!=CanonicalStateKind::DeltaRecord)||
        !same_profile(record->header,*anchor)||record->header.result!=anchor->result)return fail(Error::RecoveryUnavailable);
    return *anchor;
}
}
Result<CanonicalRestoreEvidence> SqliteJournal::canonical_restore_evidence(const CanonicalRestoreScope& scope) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);
    const auto& grant=scope.authority.proposal.grant;const auto& fenced=scope.authority.fenced_prefix;
    if(!scope.maximum_records||scope.maximum_records>1000000||!grant.epoch||fenced.epoch!=grant.epoch||
        scope.checkpoint.match!=s.config.match||!scope.checkpoint.id||!scope.checkpoint.generation)return fail(Error::InvalidArgument);
    Transaction tx{s.database};if(auto r=tx.begin(true);!r)return fail(r.error());
    // The exact current coordinator incarnation and committed grant, observed
    // in this snapshot. A restart, newer grant or takeover makes the scope stale.
    if(auto r=s.check_boot();!r)return fail(r.error());
    if(!s.bound_boot||*s.bound_boot!=scope.authority.boot)return fail(Error::StaleEpoch);
    auto stored=s.authority_state();if(!stored)return fail(stored.error());
    if(!*stored||!same_restore_authority((**stored).receipt,scope.authority))return fail(Error::StaleGeneration);
    auto prefix=s.prefix();if(!prefix)return fail(prefix.error());
    if(prefix->epoch!=grant.epoch)return fail(Error::StaleEpoch);
    // C is exactly the prefix this grant fenced. A later append means the
    // successor already advanced; a restore read for C is then stale.
    if(prefix->sequence!=fenced.sequence||prefix->tick!=fenced.tick)return fail(Error::StaleGeneration);
    auto image=checkpoint_row(s,scope.checkpoint.id);if(!image)return fail(image.error());
    if(image->ticket!=scope.checkpoint)return fail(Error::StaleGeneration);
    if(image->state!=CheckpointContentState::RecoveryVerified)return fail(Error::NotReady);
    auto capture=service_capture_row(s,scope.checkpoint);if(!capture)return fail(capture.error());
    if(!capture->progress.complete)return fail(Error::RecoveryUnavailable);
    // The anchor comes from the stored, hash-checked first content chunk and
    // is bound to the committed journal record at the checkpoint sequence.
    auto anchor=bound_checkpoint_anchor(s,*image);if(!anchor)return fail(anchor.error());
    std::uint64_t count=0;auto final_state=canonical_chain(s,grant.epoch,*prefix,*anchor,scope.maximum_records,count);
    if(!final_state)return fail(final_state.error()==Error::InvalidArgument?Error::RecoveryUnavailable:final_state.error());
    // Restoration is only for a fenced successor epoch beyond the final record.
    if(final_state->position.epoch>=grant.epoch)return fail(Error::RecoveryUnavailable);
    CanonicalRestoreEvidence evidence{};
    evidence.plan.checkpoint={*image,capture->progress,{capture->boot.term,capture->boot.nonce}};
    evidence.plan.final_epoch=final_state->position.epoch;evidence.plan.successor_epoch=grant.epoch;
    evidence.plan.final_sequence=prefix->sequence;evidence.plan.final_tick=prefix->tick;evidence.plan.final_state=final_state->result;
    evidence.anchor=*anchor;evidence.final_state=*final_state;evidence.snapshot=*prefix;
    if(auto r=s.reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    return evidence;
}
