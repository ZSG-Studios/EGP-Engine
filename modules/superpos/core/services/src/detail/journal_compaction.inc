// SPDX-License-Identifier: MIT
// Included last in the journal ordering domain. Canonical journal compaction
// behind the oldest of two recovery-verified bases, compacted append-identity
// tombstones and the application high-water backpressure inputs.
namespace {
// Retained charge of one tombstone; never larger than the record it replaces
// (every record is charged at least 128 bytes), so compaction never grows.
constexpr std::uint64_t retired_append_bytes=128;
constexpr std::uint64_t retired_digest_magic=0x53504a5254410001;
constexpr std::uint32_t compaction_maximum_records=4096;
Status compaction_tables(sqlite3* db) noexcept {
    return execute(db,
        "CREATE TABLE IF NOT EXISTS journal_compaction(match BLOB PRIMARY KEY CHECK(length(match)=8),floor BLOB NOT NULL CHECK(length(floor)=8),retired BLOB NOT NULL CHECK(length(retired)=8),forgotten BLOB CHECK(forgotten IS NULL OR length(forgotten)=8),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS journal_retired_appends(match BLOB NOT NULL,append_id BLOB NOT NULL CHECK(length(append_id)=8),epoch BLOB NOT NULL CHECK(length(epoch)=8),seq BLOB NOT NULL CHECK(length(seq)=8),tick BLOB NOT NULL CHECK(length(tick)=8),bytes BLOB NOT NULL CHECK(length(bytes)=8),digest BLOB NOT NULL CHECK(length(digest)=32),PRIMARY KEY(match,append_id),UNIQUE(match,seq),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;");
}
Status rebind(Statement& statement) noexcept {
    if(sqlite3_reset(statement.value)!=SQLITE_OK||sqlite3_clear_bindings(statement.value)!=SQLITE_OK)return fail(Error::Io);return {};
}
}
Result<SqliteJournal::Impl::CompactionState> SqliteJournal::Impl::compaction_state() noexcept {
    CompactionState state{};
    auto exists=service_table_exists(database,"journal_compaction");if(!exists)return fail(exists.error());if(!*exists)return state;
    Statement row;if(auto r=row.prepare(database,"SELECT floor,retired,forgotten FROM journal_compaction WHERE match=?1");!r)return fail(r.error());
    if(auto r=row.number(1,config.match);!r)return fail(r.error());
    auto code=sqlite3_step(row.value);if(code==SQLITE_DONE)return state;if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto floor=row.number(0),retired=row.number(1);if(!floor||!*floor||!retired)return fail(Error::RecoveryUnavailable);
    state.floor=*floor;state.retired=*retired;
    if(sqlite3_column_type(row.value,2)!=SQLITE_NULL){auto forgotten=row.number(2);if(!forgotten||!*forgotten||*forgotten>=*floor)return fail(Error::RecoveryUnavailable);state.forgotten=*forgotten;}
    code=sqlite3_step(row.value);if(code!=SQLITE_DONE)return fail(code==SQLITE_ROW?Error::RecoveryUnavailable:sql_error(code));
    return state;
}
Result<std::optional<SqliteJournal::Impl::RetiredAppend>> SqliteJournal::Impl::retired_append(std::uint64_t id) noexcept {
    auto exists=service_table_exists(database,"journal_retired_appends");if(!exists)return fail(exists.error());
    if(!*exists)return std::optional<RetiredAppend>{};
    Statement row;if(auto r=row.prepare(database,"SELECT epoch,seq,tick,bytes,digest FROM journal_retired_appends WHERE match=?1 AND append_id=?2");!r)return fail(r.error());
    if(auto r=row.number(1,config.match);!r)return fail(r.error());if(auto r=row.number(2,id);!r)return fail(r.error());
    auto code=sqlite3_step(row.value);if(code==SQLITE_DONE)return std::optional<RetiredAppend>{};if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto epoch=row.number(0),sequence=row.number(1),tick=row.number(2),bytes=row.number(3);auto digest=sqlite_blob(row.value,4,32,32);
    if(!epoch||!*epoch||!sequence||!*sequence||!tick||!bytes||*bytes>config.maximum_record_bytes||!digest)return fail(Error::RecoveryUnavailable);
    RetiredAppend result{{id,*epoch,*sequence,*tick,static_cast<std::uint32_t>(*bytes),true},{}};std::copy(digest->begin(),digest->end(),result.digest.begin());
    if(!known_fingerprint(result.digest))return fail(Error::RecoveryUnavailable);
    code=sqlite3_step(row.value);if(code!=SQLITE_DONE)return fail(code==SQLITE_ROW?Error::RecoveryUnavailable:sql_error(code));
    // A tombstone always lies strictly below the durable compaction floor.
    auto state=compaction_state();if(!state)return fail(state.error());if(*sequence>=state->floor)return fail(Error::RecoveryUnavailable);
    return std::optional<RetiredAppend>{result};
}
Result<Fingerprint> SqliteJournal::Impl::retired_digest(std::uint64_t id,Epoch epoch,std::uint64_t sequence,Tick tick,
    std::span<const std::byte> data,std::span<const std::byte> envelope) noexcept {
    // Exact stored canonical bytes and decision/effect envelope, bound to the
    // identity and position. Requires the store's bound SHA-256 digest.
    if(!checkpoint_digest)return fail(Error::NotReady);
    auto data_hash=checkpoint_hash(*this,data);if(!data_hash)return fail(data_hash.error());
    auto envelope_hash=checkpoint_hash(*this,envelope);if(!envelope_hash)return fail(envelope_hash.error());
    std::array<std::byte,128> bytes{};Writer writer(bytes);
    for(auto n:{retired_digest_magic,config.match,id,epoch,sequence,tick,std::uint64_t(data.size()),std::uint64_t(envelope.size())})if(auto r=writer.u64(n);!r)return fail(r.error());
    if(auto r=writer.raw(*data_hash);!r)return fail(r.error());if(auto r=writer.raw(*envelope_hash);!r)return fail(r.error());
    if(writer.size()!=bytes.size())return fail(Error::ProtocolViolation);
    return checkpoint_hash(*this,bytes);
}
Result<bool> SqliteJournal::Impl::recovery_runway(Epoch epoch) noexcept {
    // A successor epoch newer than every recovery-verified base needs room to
    // install its own base before compaction can release anything.
    auto exists=service_table_exists(database,"checkpoint_images");if(!exists)return fail(exists.error());if(!*exists)return false;
    Statement bases;if(auto r=bases.prepare(database,"SELECT id FROM checkpoint_images WHERE match=?1 AND state=2");!r)return fail(r.error());
    if(auto r=bases.number(1,config.match);!r)return fail(r.error());
    bool any=false;
    for(unsigned seen=0;;++seen){
        auto code=sqlite3_step(bases.value);if(code==SQLITE_DONE)break;if(code!=SQLITE_ROW)return fail(sql_error(code));
        if(seen==2)return fail(Error::RecoveryUnavailable);
        auto id=bases.number(0);if(!id)return fail(id.error());auto image=checkpoint_row(*this,*id);if(!image)return fail(image.error());
        if(image->manifest.epoch>=epoch)return false;any=true;
    }
    return any;
}
Result<std::uint64_t> SqliteJournal::Impl::wal_bytes() noexcept {
    // The active WAL handle of this connection (our bounded VFS wrapper).
    // Its size never shrinks without a TRUNCATE checkpoint, so it can only
    // overstate the live log: conservative for backpressure.
    sqlite3_file* file{};
    const auto code=sqlite3_file_control(database,"main",SQLITE_FCNTL_JOURNAL_POINTER,&file);
    if(code!=SQLITE_OK)return fail(sql_error(code));
    if(!file||!file->pMethods)return std::uint64_t{0};
    sqlite3_int64 size{};if(file->pMethods->xFileSize(file,&size)!=SQLITE_OK||size<0)return fail(Error::Io);
    return static_cast<std::uint64_t>(size);
}
Result<JournalCompactionReceipt> SqliteJournal::compact_journal(Epoch expected,std::uint32_t maximum) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(!expected||!maximum||maximum>compaction_maximum_records)return fail(Error::InvalidArgument);
    auto& s=*impl_;if(!s.checkpoint_digest||!s.bound_boot)return fail(Error::NotReady);
    Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());
    if(auto r=s.check_boot();!r)return fail(r.error());
    auto prefix=s.prefix();if(!prefix)return fail(prefix.error());
    if(prefix->epoch!=expected)return fail(Error::StaleEpoch);
    // A verified replacement exists only once two recovery-verified bases do.
    auto images=service_table_exists(s.database,"checkpoint_images");if(!images)return fail(images.error());if(!*images)return fail(Error::NotReady);
    std::array<CheckpointContentReceipt,2> bases{};std::size_t count=0;
    {
        Statement list;if(auto r=list.prepare(s.database,"SELECT id FROM checkpoint_images WHERE match=?1 AND state=2 ORDER BY id");!r)return fail(r.error());
        if(auto r=list.number(1,s.config.match);!r)return fail(r.error());
        for(;;){
            auto code=sqlite3_step(list.value);if(code==SQLITE_DONE)break;if(code!=SQLITE_ROW)return fail(sql_error(code));
            if(count==bases.size())return fail(Error::RecoveryUnavailable);
            auto id=list.number(0);if(!id)return fail(id.error());auto image=checkpoint_row(s,*id);if(!image)return fail(image.error());
            if(image->state!=CheckpointContentState::RecoveryVerified)return fail(Error::RecoveryUnavailable);
            bases[count++]=*image;
        }
    }
    if(count<bases.size())return fail(Error::NotReady);
    // Each base: complete service capture and a storage-bound canonical anchor.
    std::array<CanonicalStateHeader,2> anchors{};
    for(std::size_t i=0;i<bases.size();++i){
        auto capture=service_capture_row(s,bases[i].ticket);if(!capture)return fail(capture.error());
        if(!capture->progress.complete)return fail(Error::RecoveryUnavailable);
        auto anchor=bound_checkpoint_anchor(s,bases[i]);if(!anchor)return fail(anchor.error());anchors[i]=*anchor;
    }
    const std::size_t oldest=bases[0].manifest.sequence<=bases[1].manifest.sequence?0:1;
    // Intact committed coverage: (oldest base, C] is one canonical chain. The
    // newer base's record lies on it with its own anchor's digest (bound above).
    std::uint64_t covered=0;auto final_state=canonical_chain(s,expected,*prefix,anchors[oldest],1000000,covered);
    if(!final_state)return fail(final_state.error()==Error::InvalidArgument?Error::RecoveryUnavailable:final_state.error());
    std::uint64_t target=bases[oldest].manifest.sequence;
    {
        // An unacknowledged outbox effect keeps its record (and its envelope).
        Statement outbox;if(auto r=outbox.prepare(s.database,"SELECT MIN(seq) FROM effect_outbox WHERE match=?1");!r)return fail(r.error());
        if(auto r=outbox.number(1,s.config.match);!r)return fail(r.error());
        if(sqlite3_step(outbox.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
        if(sqlite3_column_type(outbox.value,0)!=SQLITE_NULL){auto pending=outbox.number(0);if(!pending||!*pending)return fail(Error::RecoveryUnavailable);target=std::min(target,*pending);}
    }
    auto state=s.compaction_state();if(!state)return fail(state.error());
    if(state->floor>bases[oldest].manifest.sequence)return fail(Error::RecoveryUnavailable);
    if(const std::uint64_t coverage=s.config.minimum_coverage_ticks){
        // Keep every record within the coverage window ending at C. Ticks never
        // decrease with sequence, so the window starts at one sequence.
        if(prefix->tick<coverage)target=std::min(target,state->floor);
        else{
            Statement window;if(auto r=window.prepare(s.database,"SELECT seq FROM records WHERE match=?1 AND seq>=?2 AND tick>=?3 ORDER BY seq LIMIT 1");!r)return fail(r.error());
            if(!window.number(1,s.config.match)||!window.number(2,state->floor)||!window.number(3,prefix->tick-coverage))return fail(Error::Io);
            auto code=sqlite3_step(window.value);if(code!=SQLITE_ROW)return fail(code==SQLITE_DONE?Error::RecoveryUnavailable:sql_error(code));
            auto first=window.number(0);if(!first||*first<state->floor)return fail(Error::RecoveryUnavailable);target=std::min(target,*first);
        }
    }
    JournalCompactionReceipt receipt{*prefix,bases[oldest].ticket,state->floor,target,0,0,0,0,false};
    if(state->floor>=target){
        receipt.complete=true;
        if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());
        return receipt;
    }
    if(auto r=compaction_tables(s.database);!r)return fail(r.error());
    std::uint64_t want=std::min<std::uint64_t>(maximum,target-state->floor);
    std::uint64_t retired=state->retired,released=0;std::optional<std::uint64_t> forgotten=state->forgotten;
    const std::uint64_t cap=s.config.maximum_retired_appends;
    if(retired>cap||want>cap-retired){
        // Forget the oldest tombstones of fenced older epochs only: any retry
        // of those appends is StaleEpoch, so none can execute again. Current
        // epoch tombstones are never forgotten; their compaction backpressures.
        const std::uint64_t need=std::min<std::uint64_t>(retired>cap?retired-cap+want:want-(cap-retired),compaction_maximum_records);
        std::uint64_t n=0,last=0;
        {
            Statement oldest_retired;if(auto r=oldest_retired.prepare(s.database,"SELECT seq,epoch FROM journal_retired_appends WHERE match=?1 ORDER BY seq LIMIT ?2");!r)return fail(r.error());
            if(auto r=oldest_retired.number(1,s.config.match);!r)return fail(r.error());if(sqlite3_bind_int64(oldest_retired.value,2,static_cast<sqlite3_int64>(need))!=SQLITE_OK)return fail(Error::Io);
            for(;;){
                auto code=sqlite3_step(oldest_retired.value);if(code==SQLITE_DONE)break;if(code!=SQLITE_ROW)return fail(sql_error(code));
                auto seq=oldest_retired.number(0),epoch=oldest_retired.number(1);if(!seq||!epoch||*seq<=last)return fail(Error::RecoveryUnavailable);
                if(*epoch>=prefix->epoch)break;
                last=*seq;++n;
            }
        }
        if(n){
            Statement forget;if(auto r=forget.prepare(s.database,"DELETE FROM journal_retired_appends WHERE match=?1 AND seq<=?2 AND epoch<?3");!r)return fail(r.error());
            if(!forget.number(1,s.config.match)||!forget.number(2,last)||!forget.number(3,prefix->epoch))return fail(Error::Io);
            if(sqlite3_step(forget.value)!=SQLITE_DONE)return fail(Error::Io);
            if(static_cast<std::uint64_t>(sqlite3_changes(s.database))!=n||n>retired)return fail(Error::RecoveryUnavailable);
            retired-=n;released+=n*retired_append_bytes;forgotten=std::max(forgotten.value_or(0),last);receipt.forgotten=n;
        }
        want=retired>=cap?0:std::min(want,cap-retired);
        if(!want){
            if(!n)return fail(Error::Busy);
        }
    }
    std::uint64_t floor=state->floor;
    if(want){
        auto control=service_table_exists(s.database,"control_commits");if(!control)return fail(control.error());
        Statement row,permit,tombstone,envelope,record;
        if(auto r=row.prepare(s.database,"SELECT r.append_id,r.epoch,r.tick,r.data,e.envelope FROM records r LEFT JOIN commit_envelopes e ON e.match=r.match AND e.append_id=r.append_id WHERE r.match=?1 AND r.seq=?2");!r)return fail(r.error());
        if(*control)if(auto r=permit.prepare(s.database,"SELECT 1 FROM control_commits WHERE match=?1 AND id=?2");!r)return fail(r.error());
        if(auto r=tombstone.prepare(s.database,"INSERT INTO journal_retired_appends VALUES(?1,?2,?3,?4,?5,?6,?7)");!r)return fail(r.error());
        if(auto r=envelope.prepare(s.database,"DELETE FROM commit_envelopes WHERE match=?1 AND append_id=?2");!r)return fail(r.error());
        if(auto r=record.prepare(s.database,"DELETE FROM records WHERE match=?1 AND append_id=?2 AND seq=?3");!r)return fail(r.error());
        for(std::uint64_t i=0;i<want;++i){
            const auto sequence=floor;
            if(auto r=rebind(row);!r)return fail(r.error());
            if(!row.number(1,s.config.match)||!row.number(2,sequence))return fail(Error::Io);
            auto code=sqlite3_step(row.value);if(code!=SQLITE_ROW)return fail(code==SQLITE_DONE?Error::RecoveryUnavailable:sql_error(code));
            auto id=row.number(0),epoch=row.number(1),tick=row.number(2);
            if(!id||!*id||!epoch||!*epoch||*epoch>prefix->epoch||!tick||*tick>prefix->tick||sqlite3_column_type(row.value,3)!=SQLITE_BLOB)return fail(Error::RecoveryUnavailable);
            auto data=sqlite_blob(row.value,3,0,s.config.maximum_record_bytes);if(!data)return fail(data.error());
            // Historical state-only records predate envelopes: an absent row is
            // the empty envelope, exactly as an exact retry presents it.
            Result<std::span<const std::byte>> stored=std::span<const std::byte>{};
            if(sqlite3_column_type(row.value,4)!=SQLITE_NULL)stored=sqlite_blob(row.value,4,0,s.config.maximum_record_bytes-data->size());
            if(!stored)return fail(stored.error());
            auto digest=s.retired_digest(*id,*epoch,sequence,*tick,*data,*stored);if(!digest)return fail(digest.error());
            const std::uint64_t data_bytes=data->size();std::uint64_t charge=data_bytes+stored->size()+128;
            if(auto r=rebind(row);!r)return fail(r.error());
            if(*control){
                if(auto r=rebind(permit);!r)return fail(r.error());
                if(!permit.number(1,s.config.match)||!permit.number(2,*id))return fail(Error::Io);
                code=sqlite3_step(permit.value);if(code==SQLITE_ROW)charge+=512;else if(code!=SQLITE_DONE)return fail(sql_error(code));
                if(auto r=rebind(permit);!r)return fail(r.error());
            }
            if(auto r=rebind(tombstone);!r)return fail(r.error());
            if(!tombstone.number(1,s.config.match)||!tombstone.number(2,*id)||!tombstone.number(3,*epoch)||!tombstone.number(4,sequence)||
                !tombstone.number(5,*tick)||!tombstone.number(6,data_bytes)||!tombstone.bytes(7,*digest))return fail(Error::Io);
            code=sqlite3_step(tombstone.value);if(code!=SQLITE_DONE)return fail((code&255)==SQLITE_CONSTRAINT?Error::RecoveryUnavailable:sql_error(code));
            if(auto r=rebind(envelope);!r)return fail(r.error());
            if(!envelope.number(1,s.config.match)||!envelope.number(2,*id))return fail(Error::Io);
            if(sqlite3_step(envelope.value)!=SQLITE_DONE||sqlite3_changes(s.database)>1)return fail(Error::Io);
            if(auto r=rebind(record);!r)return fail(r.error());
            if(!record.number(1,s.config.match)||!record.number(2,*id)||!record.number(3,sequence))return fail(Error::Io);
            code=sqlite3_step(record.value);if(code!=SQLITE_DONE)return fail(sql_error(code));if(sqlite3_changes(s.database)!=1)return fail(Error::RecoveryUnavailable);
            released+=charge-retired_append_bytes;++retired;++floor;++receipt.pruned;
        }
    }
    if(released>prefix->retained_bytes)return fail(Error::RecoveryUnavailable);
    const auto retained=prefix->retained_bytes-released;
    {
        Statement update;if(auto r=update.prepare(s.database,"UPDATE matches SET bytes=?2 WHERE id=?1");!r)return fail(r.error());
        if(!update.number(1,s.config.match)||!update.number(2,retained))return fail(Error::Io);
        if(sqlite3_step(update.value)!=SQLITE_DONE||sqlite3_changes(s.database)!=1)return fail(Error::RecoveryUnavailable);
    }
    {
        Statement write;if(auto r=write.prepare(s.database,"INSERT INTO journal_compaction VALUES(?1,?2,?3,?4) ON CONFLICT(match) DO UPDATE SET floor=excluded.floor,retired=excluded.retired,forgotten=excluded.forgotten");!r)return fail(r.error());
        if(!write.number(1,s.config.match)||!write.number(2,floor)||!write.number(3,retired))return fail(Error::Io);
        if(forgotten){if(auto r=write.number(4,*forgotten);!r)return fail(r.error());}else if(sqlite3_bind_null(write.value,4)!=SQLITE_OK)return fail(Error::Io);
        if(sqlite3_step(write.value)!=SQLITE_DONE)return fail(Error::Io);
    }
    auto verified=s.compaction_state();
    if(!verified||verified->floor!=floor||verified->retired!=retired||verified->forgotten!=forgotten)return fail(Error::RecoveryUnavailable);
    if(auto r=s.check_boot();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    receipt.snapshot.retained_bytes=retained;receipt.floor=floor;receipt.released_bytes=released;receipt.retired_appends=receipt.pruned;receipt.complete=floor==target;
    return receipt;
}
