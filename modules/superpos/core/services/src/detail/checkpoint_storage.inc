// SPDX-License-Identifier: MIT
// Included in the journal ordering domain. Content verification is deliberately
// distinct from complete authoritative recovery/service-snapshot verification.
namespace {
Status erase_checkpoint(sqlite3*,CheckpointTicket,CheckpointContentState) noexcept;
constexpr std::size_t checkpoint_manifest_bytes=224;
Result<std::uint64_t> checkpoint_integer(sqlite3_stmt* row,int column,std::uint64_t maximum) noexcept {
    if(sqlite3_column_type(row,column)!=SQLITE_INTEGER)return fail(Error::RecoveryUnavailable);
    const auto value=sqlite3_column_int64(row,column);
    if(value<0||static_cast<std::uint64_t>(value)>maximum)return fail(Error::RecoveryUnavailable);
    return static_cast<std::uint64_t>(value);
}
Result<std::size_t> encode_checkpoint_manifest(const CheckpointManifest& m,std::span<std::byte> bytes,bool content=true) noexcept {
    Writer writer(bytes);
    for(auto n:{std::uint64_t{0x5350534348500001},m.match,m.id,m.epoch,m.sequence,m.tick,m.bytes,std::uint64_t(m.grade)})if(auto r=writer.u64(n);!r)return fail(r.error());
    for(const auto* value:{&m.schemas,&m.simulation,&m.participants,&m.service_codec})if(auto r=writer.raw(*value);!r)return fail(r.error());
    if(content)if(auto r=writer.raw(m.content);!r)return fail(r.error());
    return writer.size();
}
Status valid_checkpoint_manifest(const CheckpointManifest& m,bool content=true) noexcept {
    if(!m.match||!m.id||!m.epoch||!m.sequence||!m.bytes||m.bytes>checkpoint_maximum_bytes||
       unsigned(m.grade)<1||unsigned(m.grade)>3||!known_fingerprint(m.schemas)||!known_fingerprint(m.simulation)||
       !known_fingerprint(m.participants)||!known_fingerprint(m.service_codec)||(content&&!known_fingerprint(m.content)))return fail(Error::InvalidArgument);
    return {};
}
Result<CheckpointManifest> decode_checkpoint_manifest(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()!=checkpoint_manifest_bytes)return fail(Error::RecoveryUnavailable);
    Reader reader(bytes);std::array<std::uint64_t,8> n{};
    for(auto& value:n){auto r=reader.u64();if(!r)return fail(r.error());value=*r;}
    if(n[0]!=0x5350534348500001||n[7]>3)return fail(Error::RecoveryUnavailable);
    CheckpointManifest m{n[1],n[2],n[3],n[4],n[5],n[6]};m.grade=static_cast<RecoveryGrade>(n[7]);
    for(auto* value:{&m.schemas,&m.simulation,&m.participants,&m.service_codec,&m.content}){auto r=reader.raw(32);if(!r)return fail(r.error());std::copy(r->begin(),r->end(),value->begin());}
    if(!reader.empty()||!valid_checkpoint_manifest(m))return fail(Error::RecoveryUnavailable);return m;
}
std::uint32_t checkpoint_chunks(const CheckpointManifest& m) noexcept {return static_cast<std::uint32_t>((m.bytes+checkpoint_chunk_bytes-1)/checkpoint_chunk_bytes);}
std::size_t checkpoint_chunk_size(const CheckpointManifest& m,std::uint32_t index) noexcept {
    const auto offset=std::uint64_t(index)*checkpoint_chunk_bytes;
    return offset<m.bytes?static_cast<std::size_t>(std::min<std::uint64_t>(checkpoint_chunk_bytes,m.bytes-offset)):0;
}
template<class Store> Status checkpoint_tables(Store& s) noexcept {
    return execute(s.database,
        "CREATE TABLE IF NOT EXISTS service_revisions(match BLOB PRIMARY KEY CHECK(length(match)=8),revision BLOB NOT NULL CHECK(length(revision)=8),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS checkpoint_settings(match BLOB PRIMARY KEY,counter BLOB NOT NULL CHECK(length(counter)=8),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS checkpoint_images(match BLOB NOT NULL,id BLOB NOT NULL,generation BLOB NOT NULL CHECK(length(generation)=8),manifest BLOB NOT NULL CHECK(length(manifest)=224),state INTEGER NOT NULL CHECK(state IN(0,1,2)),PRIMARY KEY(match,id),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS checkpoint_chunks(match BLOB NOT NULL,id BLOB NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>=0 AND ordinal<768),data BLOB NOT NULL CHECK(length(data)>0 AND length(data)<=65536),digest BLOB NOT NULL CHECK(length(digest)=32),PRIMARY KEY(match,id,ordinal),FOREIGN KEY(match,id) REFERENCES checkpoint_images(match,id)) WITHOUT ROWID;");
}
template<class Store> Result<CheckpointContentReceipt> checkpoint_row(Store& s,std::uint64_t id) noexcept {
    Statement statement;if(auto r=statement.prepare(s.database,"SELECT generation,manifest,state FROM checkpoint_images WHERE match=?1 AND id=?2");!r)return fail(r.error());
    if(auto r=statement.number(1,s.config.match);!r)return fail(r.error());if(auto r=statement.number(2,id);!r)return fail(r.error());
    auto code=sqlite3_step(statement.value);if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto generation=statement.number(0);if(!generation)return fail(generation.error());
    auto bytes=sqlite_blob(statement.value,1,checkpoint_manifest_bytes,checkpoint_manifest_bytes);if(!bytes)return fail(bytes.error());
    auto manifest=decode_checkpoint_manifest(*bytes);if(!manifest)return fail(manifest.error());
    if(!*generation||manifest->match!=s.config.match||manifest->id!=id||sqlite3_column_type(statement.value,2)!=SQLITE_INTEGER)return fail(Error::RecoveryUnavailable);
    auto state=checkpoint_integer(statement.value,2,2);if(!state)return fail(state.error());
    return CheckpointContentReceipt{{s.config.match,id,*generation},*manifest,static_cast<CheckpointContentState>(*state)};
}
template<class Store> Status checkpoint_anchor(Store& s,const CheckpointManifest& manifest) noexcept {
    if(auto r=s.check_boot();!r)return r;auto prefix=s.prefix();if(!prefix)return fail(prefix.error());
    if(prefix->epoch!=manifest.epoch)return fail(Error::StaleEpoch);
    if(prefix->sequence!=manifest.sequence||prefix->tick!=manifest.tick)return fail(Error::StaleGeneration);return {};
}
struct CheckpointCallbackGuard {bool& active;bool previous;explicit CheckpointCallbackGuard(bool& value):active(value),previous(value){active=true;}~CheckpointCallbackGuard(){active=previous;}};
template<class Store> Result<Fingerprint> checkpoint_hash(Store& s,std::span<const std::byte> bytes) noexcept {
    if(!s.checkpoint_digest)return fail(Error::NotReady);Fingerprint result{};CheckpointCallbackGuard guard(s.checkpoint_callback);
    if(auto r=s.checkpoint_digest->hash(bytes,result);!r)return fail(r.error());return result;
}
template<class Store> Result<std::size_t> checkpoint_read_data(Store& s,const CheckpointContentReceipt& image,std::uint32_t index,std::span<std::byte> output) noexcept {
    auto size=checkpoint_chunk_size(image.manifest,index);if(!size)return fail(Error::InvalidArgument);if(output.size()<size)return fail(Error::Truncated);
    Statement statement;if(auto r=statement.prepare(s.database,"SELECT data,digest FROM checkpoint_chunks WHERE match=?1 AND id=?2 AND ordinal=?3");!r)return fail(r.error());
    if(auto r=statement.number(1,image.ticket.match);!r)return fail(r.error());if(auto r=statement.number(2,image.ticket.id);!r)return fail(r.error());
    if(sqlite3_bind_int(statement.value,3,static_cast<int>(index))!=SQLITE_OK)return fail(Error::Io);
    auto code=sqlite3_step(statement.value);if(code==SQLITE_DONE)return fail(Error::RecoveryUnavailable);if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto bytes=sqlite_blob(statement.value,0,size,size);if(!bytes)return fail(bytes.error());auto hash=sqlite_blob(statement.value,1,32,32);if(!hash)return fail(hash.error());
    Fingerprint actual{};if(auto r=s.checkpoint_digest->hash(*bytes,actual);!r)return fail(r.error());if(!std::equal(actual.begin(),actual.end(),hash->begin()))return fail(Error::RecoveryUnavailable);
    std::memcpy(output.data(),bytes->data(),size);return size;
}
}
Result<Fingerprint> checkpoint_content_commitment(const CheckpointManifest& manifest,std::span<const Fingerprint> chunks,CryptographicDigest& digest) noexcept {
    if(auto r=valid_checkpoint_manifest(manifest,false);!r)return fail(r.error());if(digest.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    if(chunks.size()!=checkpoint_chunks(manifest))return fail(Error::InvalidArgument);
    std::array<std::byte,192+checkpoint_maximum_chunks*32> bytes{};auto count=encode_checkpoint_manifest(manifest,bytes,false);if(!count)return fail(count.error());
    Writer writer(std::span(bytes).subspan(*count));for(const auto& hash:chunks){if(!known_fingerprint(hash))return fail(Error::InvalidArgument);if(auto r=writer.raw(hash);!r)return fail(r.error());}
    Fingerprint result{};if(auto r=digest.hash(std::span(bytes).first(*count+writer.size()),result);!r)return fail(r.error());return result;
}
Status SqliteJournal::bind_checkpoint_digest(CryptographicDigest& digest) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);
    DigestAlgorithm algorithm;{CheckpointCallbackGuard guard(impl_->checkpoint_callback);algorithm=digest.algorithm();}
    if(algorithm!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    if(impl_->checkpoint_digest&&impl_->checkpoint_digest!=&digest)return fail(Error::ProtocolViolation);impl_->checkpoint_digest=&digest;return {};
}
Result<CheckpointContentReceipt> SqliteJournal::begin_checkpoint(const CheckpointManifest& manifest) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);
    if(auto r=valid_checkpoint_manifest(manifest);!r)return fail(r.error());if(manifest.match!=s.config.match)return fail(Error::InvalidArgument);
    Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());if(auto r=checkpoint_tables(s);!r)return fail(r.error());
    auto old=checkpoint_row(s,manifest.id);if(old){if(old->manifest!=manifest)return fail(Error::ProtocolViolation);if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return old;}
    if(old.error()!=Error::NotReady)return fail(old.error());if(auto r=checkpoint_anchor(s,manifest);!r)return fail(r.error());
    Statement limits;if(auto r=limits.prepare(s.database,"SELECT COUNT(*),COALESCE(SUM(state<2),0) FROM checkpoint_images WHERE match=?1");!r)return fail(r.error());if(auto r=limits.number(1,s.config.match);!r)return fail(r.error());
    if(sqlite3_step(limits.value)!=SQLITE_ROW)return fail(Error::Io);auto total=checkpoint_integer(limits.value,0,3),staging=checkpoint_integer(limits.value,1,1);if(!total||!staging)return fail(Error::RecoveryUnavailable);if(*total>=3||*staging)return fail(Error::CapacityExceeded);
    Statement previous;if(auto r=previous.prepare(s.database,"SELECT counter FROM checkpoint_settings WHERE match=?1");!r)return fail(r.error());if(auto r=previous.number(1,s.config.match);!r)return fail(r.error());
    auto code=sqlite3_step(previous.value);std::uint64_t generation=1;if(code==SQLITE_ROW){auto value=previous.number(0);if(!value)return fail(value.error());if(*value==UINT64_MAX)return fail(Error::CounterExhausted);generation=*value+1;}else if(code!=SQLITE_DONE)return fail(sql_error(code));
    Statement counter;if(auto r=counter.prepare(s.database,"INSERT INTO checkpoint_settings VALUES(?1,?2) ON CONFLICT(match) DO UPDATE SET counter=excluded.counter");!r)return fail(r.error());if(auto r=counter.number(1,s.config.match);!r)return fail(r.error());if(auto r=counter.number(2,generation);!r)return fail(r.error());if(sqlite3_step(counter.value)!=SQLITE_DONE)return fail(Error::Io);
    std::array<std::byte,checkpoint_manifest_bytes> bytes{};if(auto r=encode_checkpoint_manifest(manifest,bytes);!r)return fail(r.error());Statement insert;
    if(auto r=insert.prepare(s.database,"INSERT INTO checkpoint_images VALUES(?1,?2,?3,?4,0)");!r)return fail(r.error());if(auto r=insert.number(1,s.config.match);!r)return fail(r.error());if(auto r=insert.number(2,manifest.id);!r)return fail(r.error());if(auto r=insert.number(3,generation);!r)return fail(r.error());if(auto r=insert.bytes(4,bytes);!r)return fail(r.error());if(sqlite3_step(insert.value)!=SQLITE_DONE)return fail(Error::Io);
    if(auto r=checkpoint_anchor(s,manifest);!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return CheckpointContentReceipt{{manifest.match,manifest.id,generation},manifest,CheckpointContentState::Staging};
}
Status SqliteJournal::put_checkpoint_chunk(CheckpointTicket ticket,std::uint32_t index,std::span<const std::byte> bytes) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return r;
    auto row=checkpoint_row(s,ticket.id);if(!row)return fail(row.error());if(row->ticket!=ticket)return fail(Error::StaleGeneration);if(bytes.size()!=checkpoint_chunk_size(row->manifest,index)||bytes.empty())return fail(Error::InvalidArgument);
    auto hash=checkpoint_hash(s,bytes);if(!hash)return fail(hash.error());Statement previous;
    if(auto r=previous.prepare(s.database,"SELECT data,digest FROM checkpoint_chunks WHERE match=?1 AND id=?2 AND ordinal=?3");!r)return r;if(auto r=previous.number(1,ticket.match);!r)return r;if(auto r=previous.number(2,ticket.id);!r)return r;if(sqlite3_bind_int(previous.value,3,static_cast<int>(index))!=SQLITE_OK)return fail(Error::Io);
    auto code=sqlite3_step(previous.value);if(code==SQLITE_ROW){auto old=sqlite_blob(previous.value,0,bytes.size(),bytes.size());if(!old)return fail(old.error());auto old_hash=sqlite_blob(previous.value,1,32,32);if(!old_hash)return fail(old_hash.error());if(!std::equal(bytes.begin(),bytes.end(),old->begin())||!std::equal(hash->begin(),hash->end(),old_hash->begin()))return fail(Error::ProtocolViolation);if(auto r=s.reconfirm();!r)return r;return tx.commit();}
    if(code!=SQLITE_DONE)return fail(sql_error(code));if(row->state!=CheckpointContentState::Staging)return fail(Error::ProtocolViolation);if(auto r=checkpoint_anchor(s,row->manifest);!r)return r;
    Statement insert;if(auto r=insert.prepare(s.database,"INSERT INTO checkpoint_chunks VALUES(?1,?2,?3,?4,?5)");!r)return r;if(auto r=insert.number(1,ticket.match);!r)return r;if(auto r=insert.number(2,ticket.id);!r)return r;if(sqlite3_bind_int(insert.value,3,static_cast<int>(index))!=SQLITE_OK)return fail(Error::Io);if(auto r=insert.bytes(4,bytes);!r)return r;if(auto r=insert.bytes(5,*hash);!r)return r;if(sqlite3_step(insert.value)!=SQLITE_DONE)return fail(Error::Io);
    if(auto r=checkpoint_anchor(s,row->manifest);!r)return r;return tx.commit();
}
Result<CheckpointContentReceipt> SqliteJournal::seal_checkpoint(CheckpointTicket ticket) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto row=checkpoint_row(s,ticket.id);if(!row)return fail(row.error());if(row->ticket!=ticket)return fail(Error::StaleGeneration);
    if(row->state!=CheckpointContentState::Staging){if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return row;}
    if(auto r=checkpoint_anchor(s,row->manifest);!r)return fail(r.error());std::array<Fingerprint,checkpoint_maximum_chunks> hashes{};Statement chunks;
    if(auto r=chunks.prepare(s.database,"SELECT ordinal,digest,length(data) FROM checkpoint_chunks WHERE match=?1 AND id=?2 ORDER BY ordinal");!r)return fail(r.error());if(auto r=chunks.number(1,ticket.match);!r)return fail(r.error());if(auto r=chunks.number(2,ticket.id);!r)return fail(r.error());
    std::uint32_t count=0;for(;;){auto code=sqlite3_step(chunks.value);if(code==SQLITE_DONE)break;if(code!=SQLITE_ROW)return fail(sql_error(code));auto ordinal=checkpoint_integer(chunks.value,0,checkpoint_maximum_chunks-1),length=checkpoint_integer(chunks.value,2,checkpoint_chunk_bytes);if(!ordinal||!length||count>=checkpoint_chunks(row->manifest)||*ordinal!=count||*length!=checkpoint_chunk_size(row->manifest,count))return fail(Error::RecoveryUnavailable);auto hash=sqlite_blob(chunks.value,1,32,32);if(!hash)return fail(hash.error());std::copy(hash->begin(),hash->end(),hashes[count++].begin());}
    if(count!=checkpoint_chunks(row->manifest))return fail(Error::NotReady);Result<Fingerprint> root=fail(Error::NotReady);{CheckpointCallbackGuard guard(s.checkpoint_callback);root=checkpoint_content_commitment(row->manifest,std::span(hashes).first(count),*s.checkpoint_digest);}if(!root)return fail(root.error());if(*root!=row->manifest.content)return fail(Error::RecoveryUnavailable);
    Statement update;if(auto r=update.prepare(s.database,"UPDATE checkpoint_images SET state=1 WHERE match=?1 AND id=?2");!r)return fail(r.error());if(auto r=update.number(1,ticket.match);!r)return fail(r.error());if(auto r=update.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_step(update.value)!=SQLITE_DONE)return fail(Error::Io);if(auto r=checkpoint_anchor(s,row->manifest);!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());row->state=CheckpointContentState::ContentVerified;return row;
}
Result<CheckpointContentReceipt> SqliteJournal::query_checkpoint(std::uint64_t id) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());auto row=checkpoint_row(*impl_,id);if(!row)return fail(row.error());if(auto r=impl_->reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return row;
}
Result<std::size_t> SqliteJournal::read_checkpoint_chunk(CheckpointTicket ticket,std::uint32_t index,std::span<std::byte> output) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto row=checkpoint_row(s,ticket.id);if(!row)return fail(row.error());if(row->ticket!=ticket)return fail(Error::StaleGeneration);if(row->state==CheckpointContentState::Staging)return fail(Error::NotReady);
    std::array<std::byte,checkpoint_chunk_bytes> staging{};Result<std::size_t> result=fail(Error::NotReady);{CheckpointCallbackGuard guard(s.checkpoint_callback);result=checkpoint_read_data(s,*row,index,staging);}if(!result)return fail(result.error());if(output.size()<*result)return fail(Error::Truncated);if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());std::copy_n(staging.begin(),*result,output.begin());return result;
}
Status SqliteJournal::abandon_checkpoint(CheckpointTicket ticket) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;Transaction tx{s.database};if(auto r=tx.begin();!r)return r;if(auto r=s.check_boot();!r)return r;auto row=checkpoint_row(s,ticket.id);if(!row)return fail(row.error());if(row->ticket!=ticket)return fail(Error::StaleGeneration);if(row->state==CheckpointContentState::RecoveryVerified)return fail(Error::PermissionDenied);
    if(auto r=erase_checkpoint(s.database,ticket,row->state);!r)return r;if(auto r=s.check_boot();!r)return r;return tx.commit();
}
Result<CheckpointParticipantReceipt> SqliteJournal::verify_checkpoint_participants(CheckpointTicket ticket,CheckpointParticipantVerifier& verifier) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto row=checkpoint_row(s,ticket.id);if(!row)return fail(row.error());if(row->ticket!=ticket)return fail(Error::StaleGeneration);if(row->state==CheckpointContentState::Staging)return fail(Error::NotReady);
    struct Reader final:CheckpointContentReader {Impl& store;const CheckpointContentReceipt& image;Reader(Impl& s,const CheckpointContentReceipt& r):store(s),image(r){}Result<std::size_t> read(std::uint32_t i,std::span<std::byte> output) noexcept override{return checkpoint_read_data(store,image,i,output);}} reader{s,*row};
    {CheckpointCallbackGuard guard(s.checkpoint_callback);if(verifier.participant_fingerprint()!=row->manifest.participants)return fail(Error::IncompatibleSchema);if(unsigned(verifier.supported_grade())<unsigned(row->manifest.grade)||unsigned(verifier.supported_grade())>3)return fail(Error::Unsupported);if(auto r=verifier.verify(row->manifest,reader);!r)return fail(r.error());if(verifier.participant_fingerprint()!=row->manifest.participants)return fail(Error::IncompatibleSchema);if(unsigned(verifier.supported_grade())<unsigned(row->manifest.grade)||unsigned(verifier.supported_grade())>3)return fail(Error::Unsupported);}
    if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return CheckpointParticipantReceipt{*row,row->manifest.grade};
}
