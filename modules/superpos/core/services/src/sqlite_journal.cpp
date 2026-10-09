#include "superpos/sqlite_journal.hpp"
#include "superpos/journal_envelope.hpp"
#include "superpos/service/control_ordered.hpp"
#include "superpos/codec.hpp"
#include "bounded_vfs.hpp"
#include "detail/sqlite_column.hpp"
#include <sqlite3.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace superpos {
namespace {
using detail::sqlite_blob;
using detail::sqlite_text;
Status write16(Writer& writer,std::uint16_t number) noexcept {
    const std::array<std::byte,2> bytes{std::byte(number>>8),std::byte(number&255)};return writer.raw(bytes);
}
Status write8(Writer& writer,std::uint8_t number) noexcept {
    const std::array<std::byte,1> bytes{std::byte(number)};return writer.raw(bytes);
}
Error sql_error(int code) noexcept {
    switch (code & 255) {
    case SQLITE_NOMEM: return Error::OutOfMemory;
    case SQLITE_BUSY: case SQLITE_LOCKED: return Error::Busy;
    case SQLITE_FULL: case SQLITE_TOOBIG: return Error::CapacityExceeded;
    // All statements are fixed owned SQL. SQLITE_ERROR also covers rejection
    // while SQLite compiles a trigger subprogram; it is not a storage IO error.
    case SQLITE_ERROR: case SQLITE_CORRUPT: case SQLITE_NOTADB: case SQLITE_AUTH: return Error::RecoveryUnavailable;
    default: return Error::Io;
    }
}
// The service owns this ordering domain. Trigger/view programs must not alter or
// suppress its fixed SQL operations, including bootstrap INSERT OR IGNORE.
// Reject every action originating in such a program, including SELECT RAISE.
int owned_sql_authorizer(void*,int,const char*,const char*,const char*,const char* source) noexcept {
    return source?SQLITE_DENY:SQLITE_OK;
}
struct Statement {
    sqlite3_stmt* value{};
    ~Statement() { if (value) sqlite3_finalize(value); }
    Status prepare(sqlite3* db, const char* sql) noexcept {
        const auto code=sqlite3_prepare_v2(db,sql,-1,&value,nullptr);
        return code==SQLITE_OK?Status{}:Status(fail(sql_error(code)));
    }
    Status number(int column,std::uint64_t number) noexcept {
        const auto bytes=sortable_u64(number);
        const auto code=sqlite3_bind_blob(value,column,bytes.data(),8,SQLITE_TRANSIENT);
        return code==SQLITE_OK?Status{}:Status(fail(sql_error(code)));
    }
    Status bytes(int column,std::span<const std::byte> data) noexcept {
        const std::byte empty{};
        auto code=sqlite3_bind_blob(value,column,data.empty()?&empty:data.data(),static_cast<int>(data.size()),SQLITE_TRANSIENT);
        return code==SQLITE_OK?Status{}:Status(fail(sql_error(code)));
    }
    Result<std::uint64_t> number(int column) noexcept {
        auto bytes=sqlite_blob(value,column,8,8);if(!bytes)return fail(bytes.error());
        Reader reader(*bytes);return reader.u64();
    }
};
Status execute(sqlite3* db,const char* sql) noexcept {
    const auto code=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);
    return code==SQLITE_OK?Status{}:Status(fail(sql_error(code)));
}
Status control_reconfirm(sqlite3*,std::uint64_t,std::uint64_t,const service::control::ExecutionPermit&,bool) noexcept;
// Snapshot consistency is per match; global boot changes are checked separately.
Result<bool> service_revision_enabled(sqlite3* db) noexcept {
    Statement schema;if(auto r=schema.prepare(db,"SELECT type FROM sqlite_schema WHERE name='service_revisions'");!r)return fail(r.error());
    auto code=sqlite3_step(schema.value);if(code==SQLITE_DONE)return false;if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto type=sqlite_text(schema.value,0,16);if(!type||*type!="table")return fail(Error::RecoveryUnavailable);return true;
}
Result<std::uint64_t> stored_service_revision(sqlite3* db,std::uint64_t match) noexcept {
    auto enabled=service_revision_enabled(db);if(!enabled)return fail(enabled.error());if(!*enabled)return std::uint64_t{0};
    Statement read;if(auto r=read.prepare(db,"SELECT revision FROM service_revisions WHERE match=?1");!r)return fail(r.error());
    if(auto r=read.number(1,match);!r)return fail(r.error());auto code=sqlite3_step(read.value);
    if(code==SQLITE_DONE)return std::uint64_t{0};if(code!=SQLITE_ROW)return fail(sql_error(code));return read.number(0);
}
Status advance_service_revision(sqlite3* db,std::uint64_t match) noexcept {
    // Existing small-WAL users do not pay schema/storage cost until checkpoint
    // staging enables this database-wide tracking. Every connection then sees
    // the table in its transaction and advances affected match revisions.
    auto enabled=service_revision_enabled(db);if(!enabled)return fail(enabled.error());if(!*enabled)return {};
    auto current=stored_service_revision(db,match);if(!current)return fail(current.error());if(*current==UINT64_MAX)return fail(Error::CounterExhausted);
    Statement write;if(auto r=write.prepare(db,"INSERT INTO service_revisions VALUES(?1,?2) ON CONFLICT(match) DO UPDATE SET revision=excluded.revision");!r)return r;
    if(auto r=write.number(1,match);!r)return r;if(auto r=write.number(2,*current+1);!r)return r;
    return sqlite3_step(write.value)==SQLITE_DONE?Status{}:Status(fail(Error::Io));
}
struct Transaction {
    sqlite3* db{};
    bool active{};
    Statement rollback;
    // Compile cleanup before entering a transaction: preparing ROLLBACK during
    // an allocation failure can itself fail and strand the open transaction.
    ~Transaction() { if (active && !sqlite3_get_autocommit(db)) (void)sqlite3_step(rollback.value); }
    Status begin(bool deferred=false) noexcept {
        if(auto result=rollback.prepare(db,"ROLLBACK");!result)return result;
        auto result=execute(db,deferred?"BEGIN DEFERRED":"BEGIN IMMEDIATE");active=result.has_value();return result;
    }
    Status commit(std::optional<std::uint64_t> changed_match={}) noexcept {
        if(changed_match)if(auto r=advance_service_revision(db,*changed_match);!r)return r;
        auto result=execute(db,"COMMIT");
        if (result) { active=false; return {}; }
        // COMMIT errors can be uncertain in the presence of a failed VFS/storage.
        return fail(result.error()==Error::Io?Error::UnknownOutcome:result.error());
    }
};
constexpr std::size_t authority_record_size=147;
bool same_authority_proposal(const DurableAuthorityProposal& a,const DurableAuthorityProposal& b) noexcept {
    const auto& x=a.grant;const auto& y=b.grant;
    return a.expected_grant_sequence==b.expected_grant_sequence&&a.expected_epoch==b.expected_epoch&&
        x.request==y.request&&x.coordinator_term==y.coordinator_term&&x.grant_sequence==y.grant_sequence&&
        x.membership_generation==y.membership_generation&&x.epoch==y.epoch&&x.owner==y.owner&&
        x.kind==y.kind&&x.ttl_us==y.ttl_us&&x.scope==y.scope&&x.proof_generation==y.proof_generation;
}
Status valid_authority_proposal(const DurableAuthorityProposal& p) noexcept {
    const auto& g=p.grant;
    if(!g.epoch||static_cast<unsigned>(g.kind)>1||!g.ttl_us||g.ttl_us>2000000||
        std::all_of(g.scope.begin(),g.scope.end(),[](std::byte b){return b==std::byte{};}))return fail(Error::InvalidArgument);
    if(p.expected_grant_sequence){
        if(*p.expected_grant_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
        if(g.grant_sequence!=*p.expected_grant_sequence+1)return fail(Error::InvalidArgument);
    }else if(g.grant_sequence!=0)return fail(Error::InvalidArgument);
    if(g.epoch!=p.expected_epoch&&(p.expected_epoch==UINT64_MAX||g.epoch!=p.expected_epoch+1))return fail(Error::InvalidArgument);
    return {};
}
Result<std::array<std::byte,authority_record_size>> encode_authority(
    const DurableAuthorityProposal& proposal,const JournalPrefix& prefix) noexcept {
    std::array<std::byte,authority_record_size> result{};Writer writer(result);
    if(auto r=write8(writer,1);!r)return fail(r.error());
    if(auto r=write8(writer,proposal.expected_grant_sequence?1:0);!r)return fail(r.error());
    const auto& g=proposal.grant;
    const std::array<std::uint64_t,10> numbers{proposal.expected_grant_sequence.value_or(0),proposal.expected_epoch,
        g.request,g.coordinator_term,g.grant_sequence,g.membership_generation,g.epoch,g.owner,g.ttl_us,g.proof_generation};
    for(auto n:numbers)if(auto r=writer.u64(n);!r)return fail(r.error());
    if(auto r=write8(writer,static_cast<std::uint8_t>(g.kind));!r)return fail(r.error());
    if(auto r=writer.raw(g.scope);!r)return fail(r.error());
    for(auto n:std::array<std::uint64_t,4>{prefix.epoch,prefix.sequence,prefix.tick,prefix.retained_bytes})
        if(auto r=writer.u64(n);!r)return fail(r.error());
    if(writer.size()!=result.size())return fail(Error::ProtocolViolation);
    return result;
}
Result<DurableAuthorityReceipt> decode_authority(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()!=authority_record_size)return fail(Error::RecoveryUnavailable);
    Reader reader(bytes);auto version=reader.raw(1),presence=reader.raw(1);
    if(!version||!presence||(*version)[0]!=std::byte{1}||static_cast<unsigned>((*presence)[0])>1)return fail(Error::RecoveryUnavailable);
    std::array<std::uint64_t,10> numbers{};
    for(auto& n:numbers){auto r=reader.u64();if(!r)return fail(Error::RecoveryUnavailable);n=*r;}
    auto kind=reader.raw(1),scope=reader.raw(32);if(!kind||!scope||static_cast<unsigned>((*kind)[0])>1)return fail(Error::RecoveryUnavailable);
    DurableAuthorityReceipt result;auto& p=result.proposal;auto& g=p.grant;
    if((*presence)[0]==std::byte{1})p.expected_grant_sequence=numbers[0];
    else if(numbers[0]!=0)return fail(Error::RecoveryUnavailable);
    p.expected_epoch=numbers[1];g.request=numbers[2];g.coordinator_term=numbers[3];g.grant_sequence=numbers[4];
    g.membership_generation=numbers[5];g.epoch=numbers[6];g.owner=numbers[7];g.ttl_us=numbers[8];g.proof_generation=numbers[9];
    g.kind=static_cast<AuthorityKind>(static_cast<unsigned>((*kind)[0]));std::copy(scope->begin(),scope->end(),g.scope.begin());
    auto e=reader.u64(),s=reader.u64(),t=reader.u64(),b=reader.u64();
    if(!e||!s||!t||!b||!reader.empty()||!valid_authority_proposal(p)||*e!=g.epoch)return fail(Error::RecoveryUnavailable);
    result.fenced_prefix={*e,*s,*t,*b};return result;
}
}
struct SqliteJournal::Impl {
    Allocator* allocator{};
    sqlite3* database{};
    JournalConfig config{};
    detail::BoundedVfs vfs{};
    std::thread::id owner{std::this_thread::get_id()};
    std::optional<CoordinatorBootIdentity> bound_boot{};
    std::optional<service::control::IssuerIdentityFields> control_issuer{};
    std::optional<PlatformClock> control_clock{};
    std::optional<ClockSample> control_previous{};
    bool control_paused{};
    CryptographicDigest* checkpoint_digest{};
    bool checkpoint_callback{};
    Status control_observe(bool restore=false) noexcept;
    Status control_validate(const service::control::ExecutionPermit&) noexcept;
    Result<std::optional<service::control::PolicyReceipt>> control_policy(std::uint64_t,std::uint64_t) noexcept;
    bool own_thread() const noexcept { return owner==std::this_thread::get_id()&&!checkpoint_callback; }
    // A visible row is not a durability receipt after an uncertain VFS result.
    // Change one bounded per-match token so FULL/WAL COMMIT must sync again.
    // This metadata does not advance the journal or recreate retired effects.
    Status reconfirm() noexcept {
        if(auto r=execute(database,"CREATE TABLE IF NOT EXISTS journal_confirmations(match BLOB PRIMARY KEY CHECK(length(match)=8),counter BLOB NOT NULL CHECK(length(counter)=8),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID");!r)return r;
        Statement read;
        if(auto r=read.prepare(database,"SELECT counter FROM journal_confirmations WHERE match=?1");!r)return r;
        if(auto r=read.number(1,config.match);!r)return r;
        auto code=sqlite3_step(read.value);std::uint64_t next=1;
        if(code==SQLITE_ROW){
            auto current=read.number(0);if(!current)return fail(current.error());
            if(*current==UINT64_MAX)return fail(Error::CounterExhausted);
            next=*current+1;
            if(sqlite3_step(read.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);
        }else if(code!=SQLITE_DONE)return fail(sql_error(code));
        Statement write;
        if(auto r=write.prepare(database,"INSERT INTO journal_confirmations(match,counter) VALUES(?1,?2) ON CONFLICT(match) DO UPDATE SET counter=excluded.counter");!r)return r;
        if(auto r=write.number(1,config.match);!r)return r;
        if(auto r=write.number(2,next);!r)return r;
        code=sqlite3_step(write.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
        if(sqlite3_changes(database)!=1)return fail(Error::RecoveryUnavailable);
        return {};
    }
    ~Impl() { if (database) sqlite3_close_v2(database); }
    struct CoordinatorBootState { CoordinatorBootReceipt receipt{};std::uint64_t confirmation{}; };
    Result<std::optional<CoordinatorBootState>> coordinator_boot_state() noexcept {
        Statement stmt;
        if(auto r=stmt.prepare(database,"SELECT id,term,nonce,confirmation FROM coordinator_boot");!r)return fail(r.error());
        const auto code=sqlite3_step(stmt.value);
        if(code==SQLITE_DONE)return std::optional<CoordinatorBootState>{};
        if(code!=SQLITE_ROW)return fail(sql_error(code));
        if(sqlite3_column_type(stmt.value,0)!=SQLITE_INTEGER||sqlite3_column_int64(stmt.value,0)!=1)return fail(Error::RecoveryUnavailable);
        auto term=stmt.number(1),confirmation=stmt.number(3);
        if(!term)return fail(term.error());if(!confirmation)return fail(confirmation.error());
        auto nonce=sqlite_blob(stmt.value,2,32,32);if(!nonce)return fail(nonce.error());
        CoordinatorBootState result{{*term},*confirmation};
        std::memcpy(result.receipt.nonce.data(),nonce->data(),32);
        const auto end=sqlite3_step(stmt.value);
        if(end==SQLITE_ROW)return fail(Error::RecoveryUnavailable);
        if(end!=SQLITE_DONE)return fail(sql_error(end));
        if(std::all_of(result.receipt.nonce.begin(),result.receipt.nonce.end(),[](std::byte b){return b==std::byte{};}))return fail(Error::RecoveryUnavailable);
        return std::optional<CoordinatorBootState>{result};
    }
    Result<std::optional<CoordinatorBootReceipt>> coordinator_boot() noexcept {
        auto state=coordinator_boot_state();if(!state)return fail(state.error());
        if(!*state)return std::optional<CoordinatorBootReceipt>{};
        return std::optional<CoordinatorBootReceipt>{(**state).receipt};
    }
    Status check_boot() noexcept {
        auto current=coordinator_boot_state();if(!current)return fail(current.error());
        if(!*current)return bound_boot?Status(fail(Error::StaleEpoch)):Status{};
        const auto& persisted=(**current).receipt;
        if(!bound_boot||bound_boot->term!=persisted.term||bound_boot->nonce!=persisted.nonce)return fail(Error::StaleEpoch);
        return {};
    }
    struct AuthorityState { DurableAuthorityReceipt receipt{};std::uint64_t confirmation{}; };
    Result<std::optional<AuthorityState>> authority_state() noexcept {
        // Optional authority storage has no baseline WAL footprint. Legacy
        // journal/boot-only workloads keep their existing startup headroom.
        Statement schema;
        if(auto r=schema.prepare(database,"SELECT type FROM sqlite_schema WHERE name='authority_grants'");!r)return fail(r.error());
        auto present=sqlite3_step(schema.value);
        if(present==SQLITE_DONE)return std::optional<AuthorityState>{};
        if(present!=SQLITE_ROW)return fail(sql_error(present));
        auto type=sqlite_text(schema.value,0,16);if(!type)return fail(type.error());
        if(*type!="table")return fail(Error::RecoveryUnavailable);
        present=sqlite3_step(schema.value);if(present!=SQLITE_DONE)return fail(present==SQLITE_ROW?Error::RecoveryUnavailable:sql_error(present));
        Statement stmt;
        if(auto r=stmt.prepare(database,"SELECT seq,nonce,data,confirmation FROM authority_grants WHERE match=?1");!r)return fail(r.error());
        if(auto r=stmt.number(1,config.match);!r)return fail(r.error());
        auto code=sqlite3_step(stmt.value);if(code==SQLITE_DONE)return std::optional<AuthorityState>{};
        if(code!=SQLITE_ROW)return fail(sql_error(code));
        auto sequence=stmt.number(0),confirmation=stmt.number(3);
        if(!sequence)return fail(sequence.error());if(!confirmation)return fail(confirmation.error());
        auto nonce=sqlite_blob(stmt.value,1,32,32);if(!nonce)return fail(nonce.error());
        auto data=sqlite_blob(stmt.value,2,authority_record_size,authority_record_size);if(!data)return fail(data.error());
        auto decoded=decode_authority(*data);
        if(!decoded||decoded->proposal.grant.grant_sequence!=*sequence||decoded->fenced_prefix.retained_bytes>config.retention_bytes)return fail(Error::RecoveryUnavailable);
        decoded->boot.term=decoded->proposal.grant.coordinator_term;
        std::memcpy(decoded->boot.nonce.data(),nonce->data(),32);
        if(std::all_of(decoded->boot.nonce.begin(),decoded->boot.nonce.end(),[](std::byte b){return b==std::byte{};}))return fail(Error::RecoveryUnavailable);
        code=sqlite3_step(stmt.value);if(code!=SQLITE_DONE)return fail(code==SQLITE_ROW?Error::RecoveryUnavailable:sql_error(code));
        auto boot=coordinator_boot();auto head=prefix();
        if(!boot||!*boot||!head||decoded->boot.term>(**boot).term||
            (decoded->boot.term==(**boot).term&&decoded->boot.nonce!=(**boot).nonce)||
            decoded->fenced_prefix.epoch>head->epoch||decoded->fenced_prefix.sequence>head->sequence||
            decoded->fenced_prefix.tick>head->tick)return fail(Error::RecoveryUnavailable);
        return std::optional<AuthorityState>{AuthorityState{*decoded,*confirmation}};
    }
    Result<JournalPrefix> prefix() noexcept {
        Statement stmt; if (auto r=stmt.prepare(database,"SELECT epoch,seq,tick,bytes FROM matches WHERE id=?1");!r) return fail(r.error());
        if (auto r=stmt.number(1,config.match);!r) return fail(r.error());
        const auto code=sqlite3_step(stmt.value);
        if (code!=SQLITE_ROW) return fail(code==SQLITE_DONE?Error::RecoveryUnavailable:sql_error(code));
        auto e=stmt.number(0),s=stmt.number(1),t=stmt.number(2),b=stmt.number(3);
        if (!e||!s||!t||!b) return fail(Error::RecoveryUnavailable);
        if (*b>config.retention_bytes) return fail(Error::CapacityExceeded);
        const auto end=sqlite3_step(stmt.value);
        if(end!=SQLITE_DONE)return fail(end==SQLITE_ROW?Error::RecoveryUnavailable:sql_error(end));
        return JournalPrefix{*e,*s,*t,*b};
    }
    struct JournalView {JournalReceipt receipt;const std::byte* data;};
    // The statement owns this immutable blob until it is stepped/finalized.
    // Readers may confirm durability before publishing without core allocation.
    Result<JournalView> lookup_view(std::uint64_t id,Statement& stmt) noexcept {
        if (auto r=stmt.prepare(database,"SELECT epoch,seq,tick,data FROM records WHERE match=?1 AND append_id=?2");!r) return fail(r.error());
        if (auto r=stmt.number(1,config.match);!r) return fail(r.error());
        if (auto r=stmt.number(2,id);!r) return fail(r.error());
        const auto code=sqlite3_step(stmt.value);
        if (code==SQLITE_DONE) return fail(Error::NotReady);
        if (code!=SQLITE_ROW) return fail(sql_error(code));
        auto e=stmt.number(0),s=stmt.number(1),t=stmt.number(2);
        if(!e)return fail(e.error());if(!s)return fail(s.error());if(!t)return fail(t.error());
        auto data=sqlite_blob(stmt.value,3,0,config.maximum_record_bytes);if(!data)return fail(data.error());
        return JournalView{{id,*e,*s,*t,static_cast<std::uint32_t>(data->size()),true},data->data()};
    }
    Result<JournalReceipt> lookup(std::uint64_t id,std::span<const std::byte> compare) noexcept {
        Statement stmt;auto view=lookup_view(id,stmt);if(!view)return fail(view.error());
        if(compare.size()!=view->receipt.bytes || (!compare.empty()&&std::memcmp(compare.data(),view->data,compare.size())))return fail(Error::ProtocolViolation);
        return view->receipt;
    }
    Status operation_id(const DurableOperationId& id) const noexcept {
        if(id.match!=config.match||!id.actor_epoch||static_cast<unsigned>(id.kind)>2)return fail(Error::InvalidArgument);
        return {};
    }
    Status bind_id(Statement& stmt,const DurableOperationId& id) noexcept {
        const std::array<std::uint64_t,5> values{id.match,static_cast<unsigned>(id.kind),id.actor,id.actor_epoch,id.sequence};
        for(int n=0;n<5;++n)if(auto r=stmt.number(n+1,values[n]);!r)return r;
        return {};
    }
    struct Actor {
        std::uint64_t epoch{},peer{},first{},through{},issued{},bits{};
        bool have_through{},have_issued{};
    };
    Result<Actor> actor(DurableOperationId id) noexcept {
        Statement stmt;if(auto r=stmt.prepare(database,"SELECT epoch,peer,first_seq,through,issued,bits FROM operation_actors WHERE match=?1 AND kind=?2 AND actor=?3");!r)return fail(r.error());
        if(auto r=stmt.number(1,id.match);!r)return fail(r.error());
        if(auto r=stmt.number(2,static_cast<unsigned>(id.kind));!r)return fail(r.error());
        if(auto r=stmt.number(3,id.actor);!r)return fail(r.error());
        auto code=sqlite3_step(stmt.value);if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));
        auto epoch=stmt.number(0),peer=stmt.number(1),first=stmt.number(2),bits=stmt.number(5);
        if(!epoch||!peer||!first||!bits)return fail(Error::RecoveryUnavailable);
        Actor value{*epoch,*peer,*first,0,0,*bits};
        value.have_through=sqlite3_column_type(stmt.value,3)!=SQLITE_NULL;
        value.have_issued=sqlite3_column_type(stmt.value,4)!=SQLITE_NULL;
        if(value.have_through){auto n=stmt.number(3);if(!n)return fail(n.error());value.through=*n;}
        if(value.have_issued){auto n=stmt.number(4);if(!n)return fail(n.error());value.issued=*n;}
        if(!value.epoch||(value.have_through&&(!value.have_issued||value.through>value.issued||value.through<value.first))||(value.have_issued&&value.issued<value.first))return fail(Error::RecoveryUnavailable);
        return value;
    }
    Result<DurableOperationReceipt> operation(DurableOperationId id,std::span<const std::byte> request,std::span<std::byte> output) noexcept {
        Statement cache;if(auto r=cache.prepare(database,"SELECT request,result FROM operation_results WHERE match=?1 AND kind=?2 AND actor=?3 AND actor_epoch=?4 AND op_seq=?5");!r)return fail(r.error());
        if(auto r=bind_id(cache,id);!r)return fail(r.error());
        auto code=sqlite3_step(cache.value);
        if(code==SQLITE_ROW){
            auto stored_request=sqlite_blob(cache.value,0,0,4096);if(!stored_request)return fail(stored_request.error());
            auto result=sqlite_blob(cache.value,1,0,4096);if(!result)return fail(result.error());
            if(request.size()!=stored_request->size()||(!request.empty()&&std::memcmp(request.data(),stored_request->data(),request.size())))return fail(Error::ProtocolViolation);
            if(output.size()<result->size())return fail(Error::CapacityExceeded);
            if(!result->empty())std::memcpy(output.data(),result->data(),result->size());
            return DurableOperationReceipt{DurableOperationState::Committed,static_cast<std::uint32_t>(result->size())};
        }
        if(code!=SQLITE_DONE)return fail(sql_error(code));
        auto head=actor(id);if(!head)return fail(head.error());
        if(id.actor_epoch<head->epoch)return DurableOperationReceipt{DurableOperationState::Retired,0};
        if(id.actor_epoch>head->epoch)return fail(Error::StaleEpoch);
        if((head->have_through&&id.sequence<=head->through)||id.sequence<head->first)return DurableOperationReceipt{DurableOperationState::Retired,0};
        const auto base=head->have_through?head->through+1:head->first;
        if(id.sequence>=base&&id.sequence-base<64&&(head->bits&(std::uint64_t{1}<<(id.sequence-base))))return DurableOperationReceipt{DurableOperationState::Retired,0};
        Statement pending;if(auto r=pending.prepare(database,"SELECT request FROM pending_operations WHERE match=?1 AND kind=?2 AND actor=?3 AND actor_epoch=?4 AND op_seq=?5");!r)return fail(r.error());
        if(auto r=bind_id(pending,id);!r)return fail(r.error());code=sqlite3_step(pending.value);
        if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));
        auto stored_request=sqlite_blob(pending.value,0,0,4096);if(!stored_request)return fail(stored_request.error());
        if(request.size()!=stored_request->size()||(!request.empty()&&std::memcmp(request.data(),stored_request->data(),request.size())))return fail(Error::ProtocolViolation);
        return DurableOperationReceipt{DurableOperationState::Pending,0};
    }
    Result<std::array<std::uint64_t,2>> totals(const char* sql) noexcept {
        Statement stmt;if(auto r=stmt.prepare(database,sql);!r)return fail(r.error());
        if(sqlite3_step(stmt.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
        std::array<std::uint64_t,2> result{};
        for(int i=0;i<2;++i){if(sqlite3_column_type(stmt.value,i)!=SQLITE_INTEGER||sqlite3_column_int64(stmt.value,i)<0)return fail(Error::RecoveryUnavailable);result[i]=static_cast<std::uint64_t>(sqlite3_column_int64(stmt.value,i));}
        return result;
    }
    Status complete(const DurableDecision&,std::uint64_t cache_order) noexcept;
    Result<JournalReceipt> append_transaction(Epoch,std::uint64_t,Tick,std::span<const std::byte>,std::span<const DurableDecision>,std::span<const DurableEffect>,std::span<const std::byte>,const service::control::ExecutionPermit* permit=nullptr,const CanonicalStateClaim* claim=nullptr) noexcept;
};
SqliteJournal::SqliteJournal(Impl* impl) noexcept:impl_(impl) {}
SqliteJournal::SqliteJournal(SqliteJournal&& other) noexcept:impl_(std::exchange(other.impl_,nullptr)) {}
SqliteJournal& SqliteJournal::operator=(SqliteJournal&& other) noexcept {
    if (this!=&other) { this->~SqliteJournal(); new(this) SqliteJournal(std::move(other)); } return *this;
}
SqliteJournal::~SqliteJournal() {
    if (impl_) { auto* allocator=impl_->allocator; impl_->~Impl(); allocator->deallocate(impl_); }
}
Result<SqliteJournal> SqliteJournal::open(Allocator& allocator,const char* file,JournalConfig config) noexcept {
    if (!file||!file[0]||!config.match||!config.maximum_record_bytes || config.maximum_record_bytes>1024*1024 ||
        config.retention_bytes<config.maximum_record_bytes || config.retention_bytes>1024ULL*1024*1024 ||
        config.wal_high_water_bytes<64*1024 || config.wal_high_water_bytes>256ULL*1024*1024)
        return fail(Error::InvalidArgument);
    const auto& limits=config.operations;
    if(!limits.maximum_actors||limits.maximum_actors>16384||!limits.maximum_pending||limits.maximum_pending>16384||limits.pending_bytes<256||limits.pending_bytes>64*1024*1024||!limits.peer_request_bytes||limits.peer_request_bytes>64*1024||limits.result_cache_bytes<256||limits.result_cache_bytes>8*1024*1024||!limits.maximum_effects||limits.maximum_effects>2048||limits.outbox_bytes<256||limits.outbox_bytes>10*1024*1024)return fail(Error::InvalidArgument);
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Recovery);
    if (!memory) return fail(Error::OutOfMemory);
    auto* impl=new(memory) Impl; impl->allocator=&allocator; impl->config=config;
    impl->vfs.fault_injector=config.fault_injector;
    SqliteJournal result(impl);
    if (auto r=impl->vfs.initialize(config.wal_high_water_bytes);!r) return fail(r.error());
    const auto opened=sqlite3_open_v2(file,&impl->database,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,impl->vfs.name);
    if (opened!=SQLITE_OK) return fail(sql_error(opened));
    sqlite3_extended_result_codes(impl->database,1);
    const auto authorization=sqlite3_set_authorizer(impl->database,owned_sql_authorizer,nullptr);
    if(authorization!=SQLITE_OK)return fail(sql_error(authorization));
    sqlite3_busy_timeout(impl->database,0);
    sqlite3_limit(impl->database,SQLITE_LIMIT_LENGTH,2*1024*1024);
    sqlite3_limit(impl->database,SQLITE_LIMIT_SQL_LENGTH,16384);
    if (auto r=execute(impl->database,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=0; PRAGMA cache_size=-8192; PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON;");!r) return fail(r.error());
    {
        Statement stmt;
        if (auto r=stmt.prepare(impl->database,"SELECT (SELECT journal_mode FROM pragma_journal_mode),(SELECT synchronous FROM pragma_synchronous)");!r) return fail(r.error());
        if(sqlite3_step(stmt.value)!=SQLITE_ROW||sqlite3_column_int(stmt.value,1)!=2)return fail(Error::Unsupported);
        auto mode=sqlite_text(stmt.value,0,16);if(!mode)return fail(mode.error());
        if(*mode!="wal")return fail(Error::Unsupported);
    }
    // Exactly encoded unsigned blobs are lexicographically sortable across the
    // full uint64 range. Never pass identities through SQLite's signed INTEGER.
    Transaction bootstrap{impl->database};if(auto r=bootstrap.begin();!r)return fail(r.error());
    if (auto r=execute(impl->database,
        "CREATE TABLE IF NOT EXISTS coordinator_boot(id INTEGER PRIMARY KEY CHECK(id=1),term BLOB NOT NULL CHECK(length(term)=8),nonce BLOB NOT NULL CHECK(length(nonce)=32),confirmation BLOB NOT NULL CHECK(length(confirmation)=8));"
        "CREATE TABLE IF NOT EXISTS matches(id BLOB PRIMARY KEY CHECK(length(id)=8),epoch BLOB NOT NULL CHECK(length(epoch)=8),seq BLOB NOT NULL CHECK(length(seq)=8),tick BLOB NOT NULL CHECK(length(tick)=8),bytes BLOB NOT NULL CHECK(length(bytes)=8)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS records(match BLOB NOT NULL,append_id BLOB NOT NULL CHECK(length(append_id)=8),epoch BLOB NOT NULL CHECK(length(epoch)=8),seq BLOB NOT NULL CHECK(length(seq)=8),tick BLOB NOT NULL CHECK(length(tick)=8),data BLOB NOT NULL,PRIMARY KEY(match,append_id),UNIQUE(match,seq),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS operation_actors(match BLOB NOT NULL,kind BLOB NOT NULL CHECK(length(kind)=8),actor BLOB NOT NULL CHECK(length(actor)=8),epoch BLOB NOT NULL CHECK(length(epoch)=8),peer BLOB NOT NULL CHECK(length(peer)=8),first_seq BLOB NOT NULL CHECK(length(first_seq)=8),through BLOB CHECK(through IS NULL OR length(through)=8),issued BLOB CHECK(issued IS NULL OR length(issued)=8),bits BLOB NOT NULL CHECK(length(bits)=8),PRIMARY KEY(match,kind,actor),FOREIGN KEY(match) REFERENCES matches(id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS pending_operations(match BLOB NOT NULL,kind BLOB NOT NULL,actor BLOB NOT NULL,actor_epoch BLOB NOT NULL CHECK(length(actor_epoch)=8),op_seq BLOB NOT NULL CHECK(length(op_seq)=8),peer BLOB NOT NULL CHECK(length(peer)=8),request BLOB NOT NULL CHECK(length(request)<=4096),PRIMARY KEY(match,kind,actor,actor_epoch,op_seq),FOREIGN KEY(match,kind,actor) REFERENCES operation_actors(match,kind,actor)) WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS pending_peer ON pending_operations(match,peer);"
        "CREATE TABLE IF NOT EXISTS operation_results(match BLOB NOT NULL,kind BLOB NOT NULL,actor BLOB NOT NULL,actor_epoch BLOB NOT NULL,op_seq BLOB NOT NULL,request BLOB NOT NULL CHECK(length(request)<=4096),result BLOB NOT NULL CHECK(length(result)<=4096),order_token BLOB NOT NULL CHECK(length(order_token)=8),PRIMARY KEY(match,kind,actor,actor_epoch,op_seq)) WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS result_order ON operation_results(order_token);"
        "CREATE TABLE IF NOT EXISTS commit_envelopes(match BLOB NOT NULL,append_id BLOB NOT NULL,envelope BLOB NOT NULL,PRIMARY KEY(match,append_id),FOREIGN KEY(match,append_id) REFERENCES records(match,append_id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS effect_outbox(match BLOB NOT NULL,append_id BLOB NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>=0 AND ordinal<64),seq BLOB NOT NULL CHECK(length(seq)=8),recipient BLOB NOT NULL CHECK(length(recipient)=8),data BLOB NOT NULL CHECK(length(data)<=4096),PRIMARY KEY(match,append_id,ordinal),FOREIGN KEY(match,append_id) REFERENCES records(match,append_id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS operation_settings(id INTEGER PRIMARY KEY CHECK(id=1),actors INTEGER,pending INTEGER,pending_bytes INTEGER,peer_bytes INTEGER,result_bytes INTEGER,effects INTEGER,outbox_bytes INTEGER,cache_order BLOB NOT NULL CHECK(length(cache_order)=8));");!r) return fail(r.error());
    Statement init;
    if (auto r=init.prepare(impl->database,"INSERT OR IGNORE INTO matches VALUES(?1,?2,?2,?2,?2)");!r) return fail(r.error());
    if (auto r=init.number(1,config.match);!r) return fail(r.error());
    if (auto r=init.number(2,0);!r) return fail(r.error());
    const auto code=sqlite3_step(init.value); if (code!=SQLITE_DONE) return fail(sql_error(code));
    {
        Statement settings;
        if(auto r=settings.prepare(impl->database,"INSERT OR IGNORE INTO operation_settings VALUES(1,?1,?2,?3,?4,?5,?6,?7,?8)");!r)return fail(r.error());
        const std::array<std::uint32_t,7> values{limits.maximum_actors,limits.maximum_pending,limits.pending_bytes,limits.peer_request_bytes,limits.result_cache_bytes,limits.maximum_effects,limits.outbox_bytes};
        for(int i=0;i<7;++i)if(sqlite3_bind_int(settings.value,i+1,static_cast<int>(values[i]))!=SQLITE_OK)return fail(Error::Io);
        if(auto r=settings.number(8,0);!r)return fail(r.error());
        if(sqlite3_step(settings.value)!=SQLITE_DONE)return fail(Error::Io);
        Statement verify;if(auto r=verify.prepare(impl->database,"SELECT actors,pending,pending_bytes,peer_bytes,result_bytes,effects,outbox_bytes FROM operation_settings WHERE id=1");!r)return fail(r.error());
        if(sqlite3_step(verify.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
        for(int i=0;i<7;++i)if(sqlite3_column_type(verify.value,i)!=SQLITE_INTEGER||sqlite3_column_int64(verify.value,i)!=values[i])return fail(Error::IncompatibleSchema);
    }
    if (auto r=impl->prefix();!r) return fail(r.error());
    if(auto r=bootstrap.commit();!r)return fail(r.error());
    return result;
}
Result<std::uint64_t> SqliteJournal::match_identity() const noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    return impl_->config.match;
}
Result<JournalPrefix> SqliteJournal::prefix() noexcept {
    if (!impl_||!impl_->own_thread()) return fail(Error::InvalidArgument);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    auto result=impl_->prefix();if(!result)return fail(result.error());
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    return result;
}
Result<std::optional<CoordinatorBootReceipt>> SqliteJournal::coordinator_boot() noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    return impl_->coordinator_boot();
}
Result<CoordinatorBootReceipt> SqliteJournal::begin_coordinator_boot(
    std::optional<std::uint64_t> expected,std::array<std::byte,32> nonce) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(std::all_of(nonce.begin(),nonce.end(),[](std::byte b){return b==std::byte{};}))return fail(Error::InvalidArgument);
    // The value argument is an immutable local copy before SQLite/VFS callbacks.
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    auto current=impl_->coordinator_boot_state();if(!current)return fail(current.error());
    if(expected&&*expected==std::numeric_limits<std::uint64_t>::max())return fail(Error::CounterExhausted);
    const auto replacement=expected?*expected+1:0;
    const bool duplicate=*current&&(**current).receipt.nonce==nonce;
    if(duplicate){
        if((**current).receipt.term!=replacement)return fail(Error::ProtocolViolation);
    }else if(static_cast<bool>(*current)!=expected.has_value()||(*current&&(**current).receipt.term!=*expected))return fail(Error::StaleEpoch);
    // A changed confirmation value forces a real WAL write even for a retry:
    // SQLite may optimize away an otherwise byte-identical UPSERT. Confirmation
    // is scoped to the current nonce; it never wraps or changes the boot term.
    std::uint64_t confirmation=0;
    if(duplicate){
        if((**current).confirmation==std::numeric_limits<std::uint64_t>::max())return fail(Error::CounterExhausted);
        confirmation=(**current).confirmation+1;
    }
    Statement stmt;
    if(auto r=stmt.prepare(impl_->database,"INSERT INTO coordinator_boot(id,term,nonce,confirmation) VALUES(1,?1,?2,?3) ON CONFLICT(id) DO UPDATE SET term=excluded.term,nonce=excluded.nonce,confirmation=excluded.confirmation");!r)return fail(r.error());
    if(auto r=stmt.number(1,replacement);!r)return fail(r.error());
    if(auto r=stmt.bytes(2,nonce);!r)return fail(r.error());
    if(auto r=stmt.number(3,confirmation);!r)return fail(r.error());
    const auto code=sqlite3_step(stmt.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    // Validate the actual stored singleton before COMMIT, including preexisting
    // schema/trigger behavior. Never issue a receipt for swapped/changed values.
    auto stored=impl_->coordinator_boot_state();if(!stored)return fail(stored.error());
    if(!*stored||(**stored).receipt.term!=replacement||(**stored).receipt.nonce!=nonce||(**stored).confirmation!=confirmation)return fail(Error::RecoveryUnavailable);
    if(auto r=tx.commit();!r)return fail(r.error());
    return CoordinatorBootReceipt{replacement,nonce,duplicate};
}
Status SqliteJournal::bind_coordinator_boot(CoordinatorBootIdentity identity) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(std::all_of(identity.nonce.begin(),identity.nonce.end(),[](std::byte b){return b==std::byte{};}))return fail(Error::InvalidArgument);
    if(impl_->bound_boot&&*impl_->bound_boot!=identity)return fail(Error::PermissionDenied);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return r;
    auto current=impl_->coordinator_boot_state();if(!current)return fail(current.error());
    if(!*current||(**current).receipt.term!=identity.term||(**current).receipt.nonce!=identity.nonce)return fail(Error::StaleEpoch);
    if(auto r=tx.commit();!r)return r;
    impl_->bound_boot=identity;return {};
}
Result<std::optional<DurableAuthorityReceipt>> SqliteJournal::authority_grant() noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);
    auto stored=impl_->authority_state();if(!stored)return fail(stored.error());
    if(!*stored)return std::optional<DurableAuthorityReceipt>{};
    return std::optional<DurableAuthorityReceipt>{(**stored).receipt};
}
Result<DurableAuthorityReceipt> SqliteJournal::commit_authority(DurableAuthorityProposal proposal) noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(auto r=valid_authority_proposal(proposal);!r)return fail(r.error());
    if(!impl_->bound_boot)return fail(Error::NotReady);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    if(auto r=impl_->check_boot();!r)return fail(r.error());
    if(proposal.grant.coordinator_term!=impl_->bound_boot->term)return fail(Error::StaleEpoch);
    auto current=impl_->prefix();if(!current)return fail(current.error());
    auto stored=impl_->authority_state();if(!stored)return fail(stored.error());
    bool duplicate=false;std::uint64_t confirmation=0;
    JournalPrefix fenced=*current;
    if(*stored&&(**stored).receipt.proposal.grant.grant_sequence==proposal.grant.grant_sequence){
        const auto& old=(**stored).receipt;
        if(old.boot!=*impl_->bound_boot)return fail(Error::StaleEpoch);
        if(!same_authority_proposal(old.proposal,proposal))return fail(Error::ProtocolViolation);
        if(current->epoch!=proposal.grant.epoch)return fail(Error::StaleEpoch);
        if((**stored).confirmation==UINT64_MAX)return fail(Error::CounterExhausted);
        confirmation=(**stored).confirmation+1;duplicate=true;fenced=old.fenced_prefix;
    }else{
        if(proposal.expected_grant_sequence.has_value()!=static_cast<bool>(*stored)||
            (*stored&&*proposal.expected_grant_sequence!=(**stored).receipt.proposal.grant.grant_sequence))return fail(Error::StaleGeneration);
        if(current->epoch!=proposal.expected_epoch)return fail(Error::StaleEpoch);
        if(*stored&&proposal.grant.scope==(**stored).receipt.proposal.grant.scope&&
            proposal.grant.request<=(**stored).receipt.proposal.grant.request)return fail(Error::StaleGeneration);
        const bool renewal=proposal.grant.epoch==proposal.expected_epoch;
        if(renewal){
            if(!*stored)return fail(Error::NotReady);
            const auto& old=(**stored).receipt;const auto& g=old.proposal.grant;const auto& n=proposal.grant;
            if(old.boot!=*impl_->bound_boot||g.epoch!=current->epoch)return fail(Error::StaleEpoch);
            if(n.owner!=g.owner||n.kind!=g.kind||n.membership_generation!=g.membership_generation||n.scope!=g.scope)return fail(Error::ProtocolViolation);
            if(n.request<=g.request)return fail(Error::StaleGeneration);
        }else{
            if(*stored&&proposal.grant.membership_generation<(**stored).receipt.proposal.grant.membership_generation)return fail(Error::StaleGeneration);
            Statement fence;if(auto r=fence.prepare(impl_->database,"UPDATE matches SET epoch=?2 WHERE id=?1");!r)return fail(r.error());
            if(auto r=fence.number(1,impl_->config.match);!r)return fail(r.error());
            if(auto r=fence.number(2,proposal.grant.epoch);!r)return fail(r.error());
            const auto code=sqlite3_step(fence.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
            if(sqlite3_changes(impl_->database)!=1)return fail(Error::RecoveryUnavailable);
            fenced.epoch=proposal.grant.epoch;
        }
    }
    auto encoded=encode_authority(proposal,fenced);if(!encoded)return fail(encoded.error());
    if(auto r=execute(impl_->database,"CREATE TABLE IF NOT EXISTS authority_grants(match BLOB PRIMARY KEY CHECK(length(match)=8),seq BLOB NOT NULL CHECK(length(seq)=8),nonce BLOB NOT NULL CHECK(length(nonce)=32),data BLOB NOT NULL CHECK(length(data)=147),confirmation BLOB NOT NULL CHECK(length(confirmation)=8)) WITHOUT ROWID");!r)return fail(r.error());
    Statement write;
    if(auto r=write.prepare(impl_->database,"INSERT INTO authority_grants(match,seq,nonce,data,confirmation) VALUES(?1,?2,?3,?4,?5) ON CONFLICT(match) DO UPDATE SET seq=excluded.seq,nonce=excluded.nonce,data=excluded.data,confirmation=excluded.confirmation");!r)return fail(r.error());
    if(auto r=write.number(1,impl_->config.match);!r)return fail(r.error());
    if(auto r=write.number(2,proposal.grant.grant_sequence);!r)return fail(r.error());
    if(auto r=write.bytes(3,impl_->bound_boot->nonce);!r)return fail(r.error());
    if(auto r=write.bytes(4,*encoded);!r)return fail(r.error());
    if(auto r=write.number(5,confirmation);!r)return fail(r.error());
    const auto code=sqlite3_step(write.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    auto verified=impl_->authority_state();auto prefix=impl_->prefix();
    if(!verified||!*verified||!prefix)return fail(Error::RecoveryUnavailable);
    const auto& persisted=(**verified).receipt;
    if(!same_authority_proposal(persisted.proposal,proposal)||persisted.boot!=*impl_->bound_boot||
        (**verified).confirmation!=confirmation||prefix->epoch!=proposal.grant.epoch||
        persisted.fenced_prefix.epoch!=fenced.epoch||persisted.fenced_prefix.sequence!=fenced.sequence||
        persisted.fenced_prefix.tick!=fenced.tick||persisted.fenced_prefix.retained_bytes!=fenced.retained_bytes)return fail(Error::RecoveryUnavailable);
    if(auto r=impl_->check_boot();!r)return fail(r.error());
    if(auto r=tx.commit(impl_->config.match);!r)return fail(r.error());
    return DurableAuthorityReceipt{proposal,*impl_->bound_boot,fenced,duplicate};
}
Result<JournalPrefix> SqliteJournal::fence(Epoch expected,Epoch replacement) noexcept {
    if (!impl_||!impl_->own_thread()||replacement<=expected) return fail(Error::InvalidArgument);
    Transaction tx{impl_->database}; if (auto r=tx.begin();!r) return fail(r.error());
    if(auto r=impl_->check_boot();!r)return fail(r.error());
    auto current=impl_->prefix(); if (!current) return fail(current.error());
    if (current->epoch!=expected) return fail(Error::StaleEpoch);
    Statement stmt; if (auto r=stmt.prepare(impl_->database,"UPDATE matches SET epoch=?2 WHERE id=?1");!r) return fail(r.error());
    if (auto r=stmt.number(1,impl_->config.match);!r) return fail(r.error());
    if (auto r=stmt.number(2,replacement);!r) return fail(r.error());
    const auto code=sqlite3_step(stmt.value); if (code!=SQLITE_DONE) return fail(sql_error(code));
    if(auto r=impl_->check_boot();!r)return fail(r.error());
    if (auto r=tx.commit(impl_->config.match);!r) return fail(r.error());
    current->epoch=replacement; return current;
}
Result<JournalReceipt> SqliteJournal::append(Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> data) noexcept {
    if (!impl_||!impl_->own_thread()||!epoch||!id||data.size()>impl_->config.maximum_record_bytes) return fail(Error::InvalidArgument);
    return impl_->append_transaction(epoch,id,tick,data,{},{},{});
}
Result<JournalReceipt> SqliteJournal::Impl::append_transaction(Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> data,std::span<const DurableDecision> decisions,std::span<const DurableEffect> effects,std::span<const std::byte> envelope,const service::control::ExecutionPermit* permit,const CanonicalStateClaim* claim) noexcept {
    Transaction tx{database}; if (auto r=tx.begin();!r) return fail(r.error());
    if(auto r=check_boot();!r)return fail(r.error());
    if(permit)if(auto r=control_validate(*permit);!r)return fail(r.error());
    Buffer canonical(*allocator,MemoryDomain::Recovery);
    if(claim){
        // Resolve retries before reading the current head: a later append/fence
        // cannot change the identity assigned to an already committed operation.
        Statement old_statement;auto old=lookup_view(id,old_statement);
        if(!old&&old.error()!=Error::NotReady)return fail(old.error());
        CanonicalStateHeader header{claim->kind,{config.match,epoch,0,tick},
            claim->schemas,claim->simulation,claim->participants,claim->codec,
            true,claim->predecessor,claim->result};
        if(old){header.position.sequence=old->receipt.sequence;}
        else{
            auto head=prefix();if(!head)return fail(head.error());
            if(head->epoch!=epoch)return fail(Error::StaleEpoch);
            if(head->sequence==UINT64_MAX)return fail(Error::CounterExhausted);
            header.position.sequence=head->sequence+1;
            if(!head->sequence){
                if(claim->kind!=CanonicalStateKind::FullRecord)return fail(Error::ProtocolViolation);
            }else{
                Statement latest;if(auto r=latest.prepare(database,"SELECT epoch,tick,data FROM records WHERE match=?1 AND seq=?2");!r)return fail(r.error());
                if(auto r=latest.number(1,config.match);!r)return fail(r.error());
                if(auto r=latest.number(2,head->sequence);!r)return fail(r.error());
                const auto code=sqlite3_step(latest.value);if(code!=SQLITE_ROW)return fail(code==SQLITE_DONE?Error::RecoveryUnavailable:sql_error(code));
                auto source_epoch=latest.number(0),source_tick=latest.number(1);
                auto bytes=sqlite_blob(latest.value,2,0,config.maximum_record_bytes);if(!bytes)return fail(bytes.error());
                auto previous_state=decode_canonical_state(*bytes);if(!previous_state)return fail(Error::RecoveryUnavailable);
                const auto& prior=previous_state->header;
                if(!source_epoch||!source_tick||prior.position!=CanonicalStatePosition{config.match,*source_epoch,head->sequence,*source_tick}||
                    (prior.kind!=CanonicalStateKind::FullRecord&&prior.kind!=CanonicalStateKind::DeltaRecord))return fail(Error::RecoveryUnavailable);
                if(prior.schemas!=header.schemas||prior.simulation!=header.simulation||prior.participants!=header.participants||prior.codec!=header.codec||
                    prior.result!=header.predecessor||prior.position.epoch>epoch||prior.position.tick>tick)return fail(Error::ProtocolViolation);
            }
        }
        if(data.size()>config.maximum_record_bytes||canonical_state_header_bytes>config.maximum_record_bytes-data.size()||
            envelope.size()>config.maximum_record_bytes-data.size()-canonical_state_header_bytes)return fail(Error::CapacityExceeded);
        if(auto r=canonical.resize(canonical_state_header_bytes+data.size());!r)return fail(r.error());
        auto encoded=encode_canonical_state(header,data,canonical.bytes());if(!encoded)return fail(encoded.error());
        data=canonical.bytes();
    }
    auto previous=lookup(id,data);
    if (previous) {
        // Retry is immutable, including source epoch and tick. A lost reply can
        // be queried after fencing without giving the old writer fresh authority.
        if (previous->epoch!=epoch||previous->tick!=tick) return fail(Error::ProtocolViolation);
        Statement prior;if(auto r=prior.prepare(database,"SELECT envelope FROM commit_envelopes WHERE match=?1 AND append_id=?2");!r)return fail(r.error());
        if(auto r=prior.number(1,config.match);!r)return fail(r.error());if(auto r=prior.number(2,id);!r)return fail(r.error());
        auto code=sqlite3_step(prior.value);
        // Historical state-only records predate envelopes and represent an empty
        // envelope. New decision/effect bundles may never attach to them later.
        if(code==SQLITE_DONE){if(!envelope.empty())return fail(Error::ProtocolViolation);}
        else if(code==SQLITE_ROW){
            auto stored=sqlite_blob(prior.value,0,0,config.maximum_record_bytes);if(!stored)return fail(stored.error());
            if(envelope.size()!=stored->size()||(!envelope.empty()&&std::memcmp(envelope.data(),stored->data(),envelope.size())))return fail(Error::ProtocolViolation);
        }
        else return fail(sql_error(code));
        if(permit){
            if(auto r=control_reconfirm(database,config.match,id,*permit,false);!r)return fail(r.error());
            if(auto r=control_validate(*permit);!r)return fail(r.error());
            if(auto r=tx.commit(config.match);!r)return fail(r.error());
        }else{
            if(auto r=reconfirm();!r)return fail(r.error());
            if(auto r=check_boot();!r)return fail(r.error());
            if(auto r=tx.commit(config.match);!r)return fail(r.error());
        }
        return previous;
    }
    if (previous.error()!=Error::NotReady) return fail(previous.error());
    auto current=prefix(); if (!current) return fail(current.error());
    if (current->epoch!=epoch) return fail(Error::StaleEpoch);
    if (current->sequence==std::numeric_limits<std::uint64_t>::max()) return fail(Error::CounterExhausted);
    if (current->sequence && (claim ? tick<current->tick : (current->tick==std::numeric_limits<std::uint64_t>::max() || tick!=current->tick+1))) return fail(Error::InvalidArgument);
    const auto charge=data.size()+envelope.size()+128ULL+(permit?512ULL:0ULL);
    if (charge>config.retention_bytes-current->retained_bytes) return fail(Error::CapacityExceeded);
    auto outbox=totals("SELECT COUNT(*),COALESCE(SUM(length(data)+256),0) FROM effect_outbox");if(!outbox)return fail(outbox.error());
    std::uint64_t effect_bytes=0;for(const auto& effect:effects)effect_bytes+=effect.payload.size()+256;
    if((*outbox)[0]>config.operations.maximum_effects||effects.size()>config.operations.maximum_effects-(*outbox)[0]||(*outbox)[1]>config.operations.outbox_bytes||effect_bytes>config.operations.outbox_bytes-(*outbox)[1])return fail(Error::CapacityExceeded);
    Statement insert;
    if (auto r=insert.prepare(database,"INSERT INTO records VALUES(?1,?2,?3,?4,?5,?6)");!r) return fail(r.error());
    const std::array<std::uint64_t,5> values{config.match,id,epoch,current->sequence+1,tick};
    for (int n=0;n<5;++n) if (auto r=insert.number(n+1,values[n]);!r) return fail(r.error());
    const std::byte empty{};
    auto code=sqlite3_bind_blob(insert.value,6,data.empty()?&empty:data.data(),static_cast<int>(data.size()),SQLITE_TRANSIENT);
    if (code!=SQLITE_OK) return fail(sql_error(code));
    code=sqlite3_step(insert.value); if (code!=SQLITE_DONE) return fail(sql_error(code));
    Statement insert_envelope;if(auto r=insert_envelope.prepare(database,"INSERT INTO commit_envelopes VALUES(?1,?2,?3)");!r)return fail(r.error());
    if(auto r=insert_envelope.number(1,config.match);!r)return fail(r.error());if(auto r=insert_envelope.number(2,id);!r)return fail(r.error());if(auto r=insert_envelope.bytes(3,envelope);!r)return fail(r.error());
    if(sqlite3_step(insert_envelope.value)!=SQLITE_DONE)return fail(Error::Io);
    Statement cache_order;if(auto r=cache_order.prepare(database,"SELECT cache_order FROM operation_settings WHERE id=1");!r)return fail(r.error());
    if(sqlite3_step(cache_order.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
    auto order=cache_order.number(0);if(!order)return fail(order.error());
    if(decisions.size()>UINT64_MAX-*order)return fail(Error::CounterExhausted);
    for(const auto& decision:decisions)if(auto r=complete(decision,++*order);!r)return fail(r.error());
    Statement update_order;if(auto r=update_order.prepare(database,"UPDATE operation_settings SET cache_order=?1 WHERE id=1");!r)return fail(r.error());
    if(auto r=update_order.number(1,*order);!r)return fail(r.error());if(sqlite3_step(update_order.value)!=SQLITE_DONE)return fail(Error::Io);
    for(std::size_t i=0;i<effects.size();++i){
        Statement effect;if(auto r=effect.prepare(database,"INSERT INTO effect_outbox VALUES(?1,?2,?3,?4,?5,?6)");!r)return fail(r.error());
        if(auto r=effect.number(1,config.match);!r)return fail(r.error());if(auto r=effect.number(2,id);!r)return fail(r.error());if(sqlite3_bind_int(effect.value,3,static_cast<int>(i))!=SQLITE_OK)return fail(Error::Io);
        if(auto r=effect.number(4,current->sequence+1);!r)return fail(r.error());if(auto r=effect.number(5,effects[i].recipient);!r)return fail(r.error());if(auto r=effect.bytes(6,effects[i].payload);!r)return fail(r.error());
        if(sqlite3_step(effect.value)!=SQLITE_DONE)return fail(Error::Io);
    }
    Statement update;
    if (auto r=update.prepare(database,"UPDATE matches SET seq=?2,tick=?3,bytes=?4 WHERE id=?1");!r) return fail(r.error());
    if (auto r=update.number(1,config.match);!r) return fail(r.error());
    if (auto r=update.number(2,current->sequence+1);!r) return fail(r.error());
    if (auto r=update.number(3,tick);!r) return fail(r.error());
    if (auto r=update.number(4,current->retained_bytes+charge);!r) return fail(r.error());
    code=sqlite3_step(update.value); if (code!=SQLITE_DONE) return fail(sql_error(code));
    if(auto r=check_boot();!r)return fail(r.error());
    if(permit){
        if(auto r=control_reconfirm(database,config.match,id,*permit,true);!r)return fail(r.error());
        if(auto r=control_validate(*permit);!r)return fail(r.error());
    }
    if (auto r=tx.commit(config.match);!r) return fail(r.error());
    return JournalReceipt{id,epoch,current->sequence+1,tick,static_cast<std::uint32_t>(data.size()),false};
}
Result<JournalReceipt> SqliteJournal::query(std::uint64_t id,std::span<std::byte> output) noexcept {
    if (!impl_||!impl_->own_thread()||!id) return fail(Error::InvalidArgument);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    Statement stmt;auto view=impl_->lookup_view(id,stmt);if(!view)return fail(view.error());
    if(output.size()<view->receipt.bytes)return fail(Error::Truncated);
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    if(view->receipt.bytes)std::memcpy(output.data(),view->data,view->receipt.bytes);
    return view->receipt;
}
Result<CommittedJournalRecord> SqliteJournal::read_committed(Epoch expected,std::uint64_t sequence,
    std::span<std::byte> canonical,std::span<std::byte> envelope) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(!expected||!sequence)return fail(Error::InvalidArgument);
    if(!canonical.empty()&&!envelope.empty()){
        const auto a=reinterpret_cast<std::uintptr_t>(canonical.data()),b=reinterpret_cast<std::uintptr_t>(envelope.data());
        if(a<=b?b-a<canonical.size():a-b<envelope.size())return fail(Error::InvalidArgument);
    }
    auto& self=*impl_;Transaction tx{self.database};
    if(auto r=tx.begin(true);!r)return fail(r.error());
    auto prefix=self.prefix();if(!prefix)return fail(prefix.error());
    if(prefix->epoch!=expected)return fail(Error::StaleEpoch);
    if(sequence>prefix->sequence)return fail(Error::NotReady);
    Statement stmt;
    if(auto r=stmt.prepare(self.database,"SELECT r.append_id,r.epoch,r.seq,r.tick,r.data,e.envelope FROM records r LEFT JOIN commit_envelopes e ON e.match=r.match AND e.append_id=r.append_id WHERE r.match=?1 AND r.seq=?2");!r)return fail(r.error());
    if(auto r=stmt.number(1,self.config.match);!r)return fail(r.error());if(auto r=stmt.number(2,sequence);!r)return fail(r.error());
    const auto code=sqlite3_step(stmt.value);
    if(code==SQLITE_DONE)return fail(Error::RecoveryUnavailable);if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto id=stmt.number(0),epoch=stmt.number(1),seq=stmt.number(2),tick=stmt.number(3);
    if(!id||!*id||!epoch||!*epoch||*epoch>expected||!seq||*seq!=sequence||!tick||*tick>prefix->tick||sqlite3_column_type(stmt.value,4)!=SQLITE_BLOB)return fail(Error::RecoveryUnavailable);
    // Current appends persist even an empty envelope. Missing older/corrupted
    // coverage must not be interpreted as a state-only weaker recovery record.
    auto canonical_data=sqlite_blob(stmt.value,4,0,self.config.maximum_record_bytes);if(!canonical_data)return fail(canonical_data.error());
    auto envelope_data=sqlite_blob(stmt.value,5,0,self.config.maximum_record_bytes-canonical_data->size());if(!envelope_data)return fail(envelope_data.error());
    if(canonical.size()<canonical_data->size()||envelope.size()<envelope_data->size())return fail(Error::Truncated);
    if(auto r=self.reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    if(!canonical_data->empty())std::memcpy(canonical.data(),canonical_data->data(),canonical_data->size());
    if(!envelope_data->empty())std::memcpy(envelope.data(),envelope_data->data(),envelope_data->size());
    return CommittedJournalRecord{*prefix,{*id,*epoch,*seq,*tick,static_cast<std::uint32_t>(canonical_data->size()),false},static_cast<std::uint32_t>(envelope_data->size())};
}
namespace {
// Runs inside the caller's fence-checked snapshot and returns the final claim
// at prefix C. Rows are inspected in place; nothing is allocated.
template<class Store> Result<CanonicalStateHeader> canonical_chain(Store& self,Epoch expected,const JournalPrefix& prefix,
    const CanonicalStateHeader& anchor,std::uint32_t maximum,std::uint64_t& count) noexcept {
    auto chain=CanonicalStateChain::create(anchor);if(!chain)return fail(chain.error());
    const auto start=anchor.position;
    if(start.match!=self.config.match||start.epoch>expected)return fail(Error::InvalidArgument);
    if(start.sequence>prefix.sequence||start.tick>prefix.tick)return fail(Error::RecoveryUnavailable);
    count=prefix.sequence-start.sequence;
    if(count>maximum)return fail(Error::CapacityExceeded);
    // Sortable fixed-width keys keep this range exact across the full u64 domain.
    Statement stmt;
    if(auto r=stmt.prepare(self.database,"SELECT epoch,seq,tick,data FROM records WHERE match=?1 AND seq>?2 AND seq<=?3 ORDER BY seq");!r)return fail(r.error());
    if(auto r=stmt.number(1,self.config.match);!r)return fail(r.error());
    if(auto r=stmt.number(2,start.sequence);!r)return fail(r.error());
    if(auto r=stmt.number(3,prefix.sequence);!r)return fail(r.error());
    std::uint64_t seen=0;
    for(;;){
        const auto code=sqlite3_step(stmt.value);
        if(code==SQLITE_DONE)break;
        if(code!=SQLITE_ROW)return fail(sql_error(code));
        if(seen==count)return fail(Error::RecoveryUnavailable);
        auto epoch=stmt.number(0),seq=stmt.number(1),tick=stmt.number(2);
        if(!epoch||!*epoch||*epoch>expected||!seq||*seq!=start.sequence+seen+1||!tick||*tick>prefix.tick)return fail(Error::RecoveryUnavailable);
        auto data=sqlite_blob(stmt.value,3,0,self.config.maximum_record_bytes);if(!data)return fail(data.error());
        // Opaque legacy bytes, row/header disagreement or a broken digest chain
        // are missing canonical coverage, never a weaker recovery profile.
        auto view=decode_canonical_state(*data);
        if(!view||view->header.position!=CanonicalStatePosition{self.config.match,*epoch,*seq,*tick})return fail(Error::RecoveryUnavailable);
        if(auto r=chain->admit(view->header);!r)return fail(Error::RecoveryUnavailable);
        ++seen;
    }
    if(seen!=count||chain->current().position.tick!=prefix.tick)return fail(Error::RecoveryUnavailable);
    return chain->current();
}
}
Result<CanonicalJournalProof> SqliteJournal::verify_canonical_journal(Epoch expected,const CanonicalStateHeader& anchor,std::uint32_t maximum) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(!expected||!maximum||maximum>1000000)return fail(Error::InvalidArgument);
    auto& self=*impl_;
    if(auto valid=CanonicalStateChain::create(anchor);!valid)return fail(valid.error());
    if(anchor.position.match!=self.config.match||anchor.position.epoch>expected)return fail(Error::InvalidArgument);
    Transaction tx{self.database};
    if(auto r=tx.begin(true);!r)return fail(r.error());
    auto prefix=self.prefix();if(!prefix)return fail(prefix.error());
    if(prefix->epoch!=expected)return fail(Error::StaleEpoch);
    std::uint64_t count=0;auto final_state=canonical_chain(self,expected,*prefix,anchor,maximum,count);if(!final_state)return fail(final_state.error());
    if(auto r=self.reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    return CanonicalJournalProof{*prefix,*final_state,count};
}
Status SqliteJournal::register_actor(Epoch authority,OperationActorKind kind,std::uint64_t actor_id,std::uint64_t actor_epoch,PeerId peer,std::uint64_t first) noexcept {
    if(!impl_||!impl_->own_thread()||!authority||!actor_epoch||static_cast<unsigned>(kind)>2)return fail(Error::InvalidArgument);
    auto& self=*impl_;Transaction tx{self.database};if(auto r=tx.begin();!r)return r;
    if(auto r=self.check_boot();!r)return r;
    auto prefix=self.prefix();if(!prefix)return fail(prefix.error());if(prefix->epoch!=authority)return fail(Error::StaleEpoch);
    DurableOperationId id{self.config.match,actor_id,actor_epoch,first,kind};auto old=self.actor(id);
    if(old){
        if(actor_epoch<old->epoch)return fail(Error::StaleEpoch);
        if(actor_epoch==old->epoch){
            if(old->peer!=peer||old->first!=first)return fail(Error::ProtocolViolation);
            if(auto r=self.reconfirm();!r)return r;
            if(auto r=self.check_boot();!r)return r;
            return tx.commit(impl_->config.match);
        }
        Statement pending;if(auto r=pending.prepare(self.database,"SELECT 1 FROM pending_operations WHERE match=?1 AND kind=?2 AND actor=?3 LIMIT 1");!r)return r;
        if(auto r=pending.number(1,id.match);!r)return r;if(auto r=pending.number(2,static_cast<unsigned>(kind));!r)return r;if(auto r=pending.number(3,actor_id);!r)return r;
        auto code=sqlite3_step(pending.value);if(code==SQLITE_ROW)return fail(Error::Busy);if(code!=SQLITE_DONE)return fail(sql_error(code));
    }else{
        if(old.error()!=Error::NotReady)return fail(old.error());
        auto total=self.totals("SELECT COUNT(*),0 FROM operation_actors");if(!total)return fail(total.error());
        if((*total)[0]>=self.config.operations.maximum_actors)return fail(Error::CapacityExceeded);
    }
    Statement write;if(auto r=write.prepare(self.database,"INSERT INTO operation_actors VALUES(?1,?2,?3,?4,?5,?6,NULL,NULL,?7) ON CONFLICT(match,kind,actor) DO UPDATE SET epoch=excluded.epoch,peer=excluded.peer,first_seq=excluded.first_seq,through=NULL,issued=NULL,bits=excluded.bits");!r)return r;
    const std::array<std::uint64_t,7> values{id.match,static_cast<unsigned>(kind),actor_id,actor_epoch,peer,first,0};
    for(int i=0;i<7;++i)if(auto r=write.number(i+1,values[i]);!r)return r;
    auto code=sqlite3_step(write.value);if(code!=SQLITE_DONE)return fail(sql_error(code));if(auto r=self.check_boot();!r)return r;
    return tx.commit(impl_->config.match);
}
Result<DurableOperationReceipt> SqliteJournal::query_operation(DurableOperationId id,std::span<const std::byte> request,std::span<std::byte> output) noexcept {
    if(!impl_||!impl_->own_thread()||request.size()>4096)return fail(Error::InvalidArgument);
    if(auto r=impl_->operation_id(id);!r)return fail(r.error());
    std::array<std::byte,4096> staged{};
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    auto result=impl_->operation(id,request,std::span(staged).first(std::min(output.size(),staged.size())));
    if(!result)return fail(result.error());
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    if(result->result_bytes)std::memcpy(output.data(),staged.data(),result->result_bytes);
    return result;
}
Result<DurableOperationReceipt> SqliteJournal::begin_operation(Epoch authority,DurableOperationId id,std::span<const std::byte> request,std::span<std::byte> output) noexcept {
    if(!impl_||!impl_->own_thread()||!authority||request.size()>4096)return fail(Error::InvalidArgument);
    auto& self=*impl_;if(auto r=self.operation_id(id);!r)return fail(r.error());
    if(self.config.maximum_record_bytes<41+request.size())return fail(Error::Unsupported);
    Transaction tx{self.database};if(auto r=tx.begin();!r)return fail(r.error());
    if(auto r=self.check_boot();!r)return fail(r.error());
    std::array<std::byte,4096> staged{};
    auto prior=self.operation(id,request,std::span(staged).first(std::min(output.size(),staged.size())));
    if(prior&&(prior->state==DurableOperationState::Committed||prior->state==DurableOperationState::Retired)){
        if(auto r=self.reconfirm();!r)return fail(r.error());
        if(auto r=self.check_boot();!r)return fail(r.error());
        if(auto r=tx.commit(impl_->config.match);!r)return fail(r.error());
        if(prior->result_bytes)std::memcpy(output.data(),staged.data(),prior->result_bytes);
        return prior;
    }
    if(!prior&&prior.error()!=Error::NotReady)return fail(prior.error());
    auto prefix=self.prefix();if(!prefix)return fail(prefix.error());if(prefix->epoch!=authority)return fail(Error::StaleEpoch);
    if(prior){
        if(auto r=self.reconfirm();!r)return fail(r.error());
        if(auto r=self.check_boot();!r)return fail(r.error());
        if(auto r=tx.commit(impl_->config.match);!r)return fail(r.error());
        return prior;
    }
    auto head=self.actor(id);if(!head)return fail(head.error());if(head->epoch!=id.actor_epoch)return fail(Error::StaleEpoch);
    if(head->have_through&&head->through==UINT64_MAX)return fail(Error::CounterExhausted);
    const auto base=head->have_through?head->through+1:head->first;
    if(id.sequence<base||id.sequence-base>=64)return fail(Error::CapacityExceeded);
    auto totals=self.totals("SELECT COUNT(*),COALESCE(SUM(length(request)+256),0) FROM pending_operations");if(!totals)return fail(totals.error());
    const auto& limits=self.config.operations;
    if((*totals)[0]>=limits.maximum_pending||(*totals)[1]>limits.pending_bytes||request.size()+256>limits.pending_bytes-(*totals)[1])return fail(Error::CapacityExceeded);
    Statement peer;if(auto r=peer.prepare(self.database,"SELECT COALESCE(SUM(length(request)),0) FROM pending_operations WHERE match=?1 AND peer=?2");!r)return fail(r.error());
    if(auto r=peer.number(1,id.match);!r)return fail(r.error());if(auto r=peer.number(2,head->peer);!r)return fail(r.error());
    if(sqlite3_step(peer.value)!=SQLITE_ROW||sqlite3_column_type(peer.value,0)!=SQLITE_INTEGER)return fail(Error::RecoveryUnavailable);
    auto peer_bytes=sqlite3_column_int64(peer.value,0);if(peer_bytes<0||static_cast<std::uint64_t>(peer_bytes)>limits.peer_request_bytes||request.size()>limits.peer_request_bytes-static_cast<std::uint64_t>(peer_bytes))return fail(Error::CapacityExceeded);
    Statement insert;if(auto r=insert.prepare(self.database,"INSERT INTO pending_operations VALUES(?1,?2,?3,?4,?5,?6,?7)");!r)return fail(r.error());
    if(auto r=self.bind_id(insert,id);!r)return fail(r.error());if(auto r=insert.number(6,head->peer);!r)return fail(r.error());if(auto r=insert.bytes(7,request);!r)return fail(r.error());
    auto code=sqlite3_step(insert.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    Statement update;if(auto r=update.prepare(self.database,"UPDATE operation_actors SET issued=?4 WHERE match=?1 AND kind=?2 AND actor=?3");!r)return fail(r.error());
    if(auto r=update.number(1,id.match);!r)return fail(r.error());if(auto r=update.number(2,static_cast<unsigned>(id.kind));!r)return fail(r.error());if(auto r=update.number(3,id.actor);!r)return fail(r.error());if(auto r=update.number(4,head->have_issued?std::max(head->issued,id.sequence):id.sequence);!r)return fail(r.error());
    if(sqlite3_step(update.value)!=SQLITE_DONE)return fail(Error::Io);
    if(auto r=self.check_boot();!r)return fail(r.error());
    if(auto r=tx.commit(impl_->config.match);!r)return fail(r.error());return DurableOperationReceipt{DurableOperationState::Execute,0};
}
Result<std::size_t> SqliteJournal::pending_operations(Epoch authority,std::span<PendingOperation> output,std::optional<DurableOperationId> after) noexcept {
    if(!impl_||!impl_->own_thread()||!authority||output.size()>64)return fail(Error::InvalidArgument);
    if(after)if(auto r=impl_->operation_id(*after);!r)return fail(r.error());
    // A read transaction keeps fence and queue observations in one snapshot.
    Transaction tx{impl_->database};if(auto r=tx.begin(true);!r)return fail(r.error());
    auto prefix=impl_->prefix();if(!prefix)return fail(prefix.error());if(prefix->epoch!=authority)return fail(Error::StaleEpoch);
    if(output.empty()){
        if(auto r=impl_->reconfirm();!r)return fail(r.error());
        if(auto r=tx.commit();!r)return fail(r.error());
        return std::size_t{0};
    }
    Buffer scratch(*impl_->allocator,MemoryDomain::Recovery);if(auto r=scratch.resize(output.size()*sizeof(PendingOperation));!r)return fail(r.error());
    auto* staged=reinterpret_cast<PendingOperation*>(scratch.bytes().data());for(std::size_t i=0;i<output.size();++i)std::construct_at(staged+i);
    Statement stmt;if(auto r=stmt.prepare(impl_->database,"SELECT kind,actor,actor_epoch,op_seq,peer,request FROM pending_operations WHERE match=?1 AND (?2=0 OR (kind,actor,actor_epoch,op_seq)>(?3,?4,?5,?6)) ORDER BY kind,actor,actor_epoch,op_seq LIMIT ?7");!r)return fail(r.error());
    if(auto r=stmt.number(1,impl_->config.match);!r)return fail(r.error());if(sqlite3_bind_int(stmt.value,2,after?1:0)!=SQLITE_OK)return fail(Error::Io);
    const DurableOperationId cursor=after.value_or(DurableOperationId{});
    const std::array<std::uint64_t,4> values{static_cast<unsigned>(cursor.kind),cursor.actor,cursor.actor_epoch,cursor.sequence};
    for(int i=0;i<4;++i)if(auto r=stmt.number(i+3,values[i]);!r)return fail(r.error());if(sqlite3_bind_int(stmt.value,7,static_cast<int>(output.size()))!=SQLITE_OK)return fail(Error::Io);
    std::size_t count=0;
    for(;;){auto code=sqlite3_step(stmt.value);if(code==SQLITE_DONE)break;if(code!=SQLITE_ROW)return fail(sql_error(code));if(count==output.size())return fail(Error::RecoveryUnavailable);
        auto kind=stmt.number(0),actor=stmt.number(1),epoch=stmt.number(2),sequence=stmt.number(3),peer=stmt.number(4);
        if(!kind||*kind>2||!actor||!epoch||!*epoch||!sequence||!peer)return fail(Error::RecoveryUnavailable);
        auto request=sqlite_blob(stmt.value,5,0,4096);if(!request)return fail(request.error());
        auto& item=staged[count++];item.id={impl_->config.match,*actor,*epoch,*sequence,static_cast<OperationActorKind>(*kind)};item.peer=*peer;item.request_bytes=static_cast<std::uint16_t>(request->size());if(!request->empty())std::memcpy(item.request.data(),request->data(),request->size());
    }
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    for(std::size_t i=0;i<count;++i)output[i]=staged[i];return count;
}
Status SqliteJournal::Impl::complete(const DurableDecision& decision,std::uint64_t order) noexcept {
    std::array<std::byte,4096> scratch{};
    auto status=operation(decision.id,decision.request,scratch);if(!status)return fail(status.error());if(status->state!=DurableOperationState::Pending)return fail(Error::ProtocolViolation);
    auto head=actor(decision.id);if(!head)return fail(head.error());if(head->epoch!=decision.id.actor_epoch)return fail(Error::StaleEpoch);
    if(!head->have_issued||decision.id.sequence>head->issued||(head->have_through&&head->through==UINT64_MAX))return fail(Error::ProtocolViolation);
    const auto base=head->have_through?head->through+1:head->first;
    if(decision.id.sequence<base||decision.id.sequence-base>=64)return fail(Error::ProtocolViolation);
    head->bits|=std::uint64_t{1}<<(decision.id.sequence-base);
    while(head->bits&1){if(head->have_through){if(head->through==UINT64_MAX)return fail(Error::CounterExhausted);++head->through;}else{head->through=head->first;head->have_through=true;}head->bits>>=1;}
    Statement update;if(auto r=update.prepare(database,"UPDATE operation_actors SET through=?4,bits=?5 WHERE match=?1 AND kind=?2 AND actor=?3");!r)return r;
    if(auto r=update.number(1,decision.id.match);!r)return r;if(auto r=update.number(2,static_cast<unsigned>(decision.id.kind));!r)return r;if(auto r=update.number(3,decision.id.actor);!r)return r;
    if(head->have_through){if(auto r=update.number(4,head->through);!r)return r;}else if(sqlite3_bind_null(update.value,4)!=SQLITE_OK)return fail(Error::Io);
    if(auto r=update.number(5,head->bits);!r)return r;if(sqlite3_step(update.value)!=SQLITE_DONE)return fail(Error::Io);
    Statement remove;if(auto r=remove.prepare(database,"DELETE FROM pending_operations WHERE match=?1 AND kind=?2 AND actor=?3 AND actor_epoch=?4 AND op_seq=?5");!r)return r;
    if(auto r=bind_id(remove,decision.id);!r)return r;if(sqlite3_step(remove.value)!=SQLITE_DONE)return fail(Error::Io);
    const auto charge=decision.request.size()+decision.result.size()+256;
    if(charge<=config.operations.result_cache_bytes){
        auto cache=totals("SELECT COUNT(*),COALESCE(SUM(length(request)+length(result)+256),0) FROM operation_results");if(!cache)return fail(cache.error());
        while((*cache)[1]>config.operations.result_cache_bytes-charge){
            Statement victim;if(auto r=victim.prepare(database,"SELECT match,kind,actor,actor_epoch,op_seq,order_token FROM operation_results ORDER BY order_token,match,kind,actor,actor_epoch,op_seq LIMIT 1");!r)return r;
            if(sqlite3_step(victim.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
            std::array<std::uint64_t,6> key{};for(int i=0;i<6;++i){auto value=victim.number(i);if(!value)return fail(value.error());key[i]=*value;}
            Statement evict;if(auto r=evict.prepare(database,"DELETE FROM operation_results WHERE match=?1 AND kind=?2 AND actor=?3 AND actor_epoch=?4 AND op_seq=?5 AND order_token=?6");!r)return r;
            for(int i=0;i<6;++i)if(auto r=evict.number(i+1,key[i]);!r)return r;
            if(sqlite3_step(evict.value)!=SQLITE_DONE)return fail(Error::Io);if(sqlite3_changes(database)!=1)return fail(Error::RecoveryUnavailable);
            if(key[0]!=config.match)if(auto r=advance_service_revision(database,key[0]);!r)return r;
            cache=totals("SELECT COUNT(*),COALESCE(SUM(length(request)+length(result)+256),0) FROM operation_results");if(!cache)return fail(cache.error());
        }
        Statement insert;if(auto r=insert.prepare(database,"INSERT INTO operation_results VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");!r)return r;
        if(auto r=bind_id(insert,decision.id);!r)return r;if(auto r=insert.bytes(6,decision.request);!r)return r;if(auto r=insert.bytes(7,decision.result);!r)return r;if(auto r=insert.number(8,order);!r)return r;
        if(sqlite3_step(insert.value)!=SQLITE_DONE)return fail(Error::Io);
    }
    return {};
}
Result<JournalReceipt> SqliteJournal::append_decisions(Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> canonical,std::span<const DurableDecision> decisions,std::span<const DurableEffect> effects) noexcept {
    return append_bundle(epoch,id,tick,canonical,decisions,effects,nullptr);
}
Result<JournalReceipt> SqliteJournal::append_canonical(Epoch epoch,std::uint64_t id,Tick tick,CanonicalStateClaim claim,std::span<const std::byte> payload,std::span<const DurableDecision> decisions,std::span<const DurableEffect> effects) noexcept {
    if(claim.kind!=CanonicalStateKind::FullRecord&&claim.kind!=CanonicalStateKind::DeltaRecord)return fail(Error::InvalidArgument);
    return append_bundle(epoch,id,tick,payload,decisions,effects,&claim);
}
Result<JournalReceipt> SqliteJournal::append_bundle(Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> canonical,std::span<const DurableDecision> decisions,std::span<const DurableEffect> effects,const CanonicalStateClaim* claim) noexcept {
    if(!impl_||!impl_->own_thread()||!epoch||!id||canonical.size()>impl_->config.maximum_record_bytes||decisions.size()>64||effects.size()>64)return fail(Error::InvalidArgument);
    if(decisions.empty()&&effects.empty())return impl_->append_transaction(epoch,id,tick,canonical,{},{},{},nullptr,claim);
    std::size_t bytes=4;
    for(std::size_t i=0;i<decisions.size();++i){const auto& decision=decisions[i];if(auto r=impl_->operation_id(decision.id);!r)return fail(r.error());if(decision.request.size()>4096||decision.result.size()>4096)return fail(Error::CapacityExceeded);for(std::size_t j=0;j<i;++j)if(decisions[j].id==decision.id)return fail(Error::ProtocolViolation);bytes+=37+decision.request.size()+decision.result.size();}
    for(const auto& effect:effects){if(effect.payload.size()>4096)return fail(Error::CapacityExceeded);bytes+=10+effect.payload.size();}
    // The complete canonical tick journal includes decisions and outbox data;
    // the one-MiB default is not a separate allowance for each component.
    if(bytes>impl_->config.maximum_record_bytes-canonical.size())return fail(Error::CapacityExceeded);
    Buffer envelope(*impl_->allocator,MemoryDomain::Recovery);if(auto r=envelope.resize(bytes);!r)return fail(r.error());Writer writer(envelope.bytes());
    if(!write16(writer,static_cast<std::uint16_t>(decisions.size()))||!write16(writer,static_cast<std::uint16_t>(effects.size())))return fail(Error::CapacityExceeded);
    for(const auto& decision:decisions){if(!writer.u64(decision.id.match)||!write8(writer,static_cast<std::uint8_t>(decision.id.kind))||!writer.u64(decision.id.actor)||!writer.u64(decision.id.actor_epoch)||!writer.u64(decision.id.sequence)||!write16(writer,static_cast<std::uint16_t>(decision.request.size()))||!write16(writer,static_cast<std::uint16_t>(decision.result.size()))||!writer.raw(decision.request)||!writer.raw(decision.result))return fail(Error::CapacityExceeded);}
    for(const auto& effect:effects)if(!writer.u64(effect.recipient)||!write16(writer,static_cast<std::uint16_t>(effect.payload.size()))||!writer.raw(effect.payload))return fail(Error::CapacityExceeded);
    return impl_->append_transaction(epoch,id,tick,canonical,decisions,effects,envelope.bytes(),nullptr,claim);
}
Result<DurableEffectReceipt> SqliteJournal::next_effect(std::span<std::byte> output) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::InvalidArgument);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());
    Statement stmt;if(auto r=stmt.prepare(impl_->database,"SELECT append_id,ordinal,seq,recipient,data FROM effect_outbox WHERE match=?1 ORDER BY seq,ordinal LIMIT 1");!r)return fail(r.error());
    if(auto r=stmt.number(1,impl_->config.match);!r)return fail(r.error());auto code=sqlite3_step(stmt.value);
    if(code==SQLITE_DONE){
        if(auto r=impl_->reconfirm();!r)return fail(r.error());
        if(auto r=tx.commit();!r)return fail(r.error());
        return fail(Error::NotReady);
    }
    if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto id=stmt.number(0),seq=stmt.number(2),recipient=stmt.number(3);auto ordinal=sqlite3_column_int(stmt.value,1);
    if(!id||!seq||!recipient||sqlite3_column_type(stmt.value,1)!=SQLITE_INTEGER||ordinal<0||ordinal>=64)return fail(Error::RecoveryUnavailable);
    auto data=sqlite_blob(stmt.value,4,0,4096);if(!data)return fail(data.error());
    if(output.size()<data->size())return fail(Error::CapacityExceeded);
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());
    if(!data->empty())std::memcpy(output.data(),data->data(),data->size());
    return DurableEffectReceipt{impl_->config.match,*id,*seq,*recipient,static_cast<std::uint32_t>(ordinal),static_cast<std::uint32_t>(data->size())};
}
Status SqliteJournal::acknowledge_effect(std::uint64_t id,std::uint32_t ordinal,std::uint64_t recipient) noexcept {
    if(!impl_||!impl_->own_thread()||!id||ordinal>=64)return fail(Error::InvalidArgument);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return r;
    if(auto r=impl_->check_boot();!r)return r;
    Statement stmt;if(auto r=stmt.prepare(impl_->database,"DELETE FROM effect_outbox WHERE match=?1 AND append_id=?2 AND ordinal=?3 AND recipient=?4");!r)return r;
    if(auto r=stmt.number(1,impl_->config.match);!r)return r;if(auto r=stmt.number(2,id);!r)return r;if(sqlite3_bind_int(stmt.value,3,static_cast<int>(ordinal))!=SQLITE_OK)return fail(Error::Io);if(auto r=stmt.number(4,recipient);!r)return r;
    auto code=sqlite3_step(stmt.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    if(!sqlite3_changes(impl_->database)){
        Statement exists;if(auto r=exists.prepare(impl_->database,"SELECT 1 FROM effect_outbox WHERE match=?1 AND append_id=?2 AND ordinal=?3");!r)return r;
        if(auto r=exists.number(1,impl_->config.match);!r)return r;if(auto r=exists.number(2,id);!r)return r;if(sqlite3_bind_int(exists.value,3,static_cast<int>(ordinal))!=SQLITE_OK)return fail(Error::Io);
        code=sqlite3_step(exists.value);if(code==SQLITE_ROW)return fail(Error::PermissionDenied);if(code!=SQLITE_DONE)return fail(sql_error(code));
        if(auto r=impl_->reconfirm();!r)return r;
    }
    if(auto r=impl_->check_boot();!r)return r;
    return tx.commit(impl_->config.match);
}
Status SqliteJournal::checkpoint() noexcept {
    if (!impl_||!impl_->own_thread()) return fail(Error::InvalidArgument);
    const auto code=sqlite3_wal_checkpoint_v2(impl_->database,nullptr,SQLITE_CHECKPOINT_TRUNCATE,nullptr,nullptr);
    return code==SQLITE_OK?Status{}:Status(fail(sql_error(code)));
}
#include "detail/control_storage.inc"
#include "detail/checkpoint_storage.inc"
#include "detail/checkpoint_service.inc"
#include "detail/checkpoint_replacement.inc"
#include "detail/canonical_restore.inc"
}
