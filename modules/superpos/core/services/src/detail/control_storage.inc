// SPDX-License-Identifier: MIT
// Included only in the private copied SqliteJournal translation unit. Its fixed
// SQL, boot guards and BEGIN IMMEDIATE ordering domain are shared with appends.
namespace {
using namespace service::control;
constexpr std::size_t policy_bytes=139,execution_bytes=201,binding_bytes=243;
bool nonzero_scope(const LeaseScope& s) noexcept {return std::any_of(s.begin(),s.end(),[](std::byte b){return b!=std::byte{};});}
Status table_exists(sqlite3* db,const char* name) noexcept {
    Statement q;if(auto r=q.prepare(db,"SELECT type FROM sqlite_schema WHERE name=?1");!r)return r;
    if(sqlite3_bind_text(q.value,1,name,-1,SQLITE_STATIC)!=SQLITE_OK)return fail(Error::Io);
    auto code=sqlite3_step(q.value);if(code==SQLITE_DONE)return fail(Error::NotReady);
    if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto type=sqlite_text(q.value,0,16);if(!type)return fail(type.error());
    if(*type!="table")return fail(Error::RecoveryUnavailable);
    return sqlite3_step(q.value)==SQLITE_DONE?Status{}:Status(fail(Error::RecoveryUnavailable));
}
Status valid_policy(const PrincipalPolicy& p) noexcept {
    if(!p.match||!p.session||!p.principal||!p.authority_epoch||static_cast<unsigned>(p.authority_kind)>1||
       !p.permissions||(p.permissions&~15ULL))return fail(Error::InvalidArgument);
    return {};
}
Status write_policy(Writer& w,const PrincipalPolicy& p) noexcept {
    for(auto v:std::array<std::uint64_t,6>{p.match,p.session,p.principal,p.principal_epoch,p.authority_epoch,p.authority_owner})if(auto r=w.u64(v);!r)return r;
    if(auto r=write8(w,static_cast<std::uint8_t>(p.authority_kind));!r)return r;
    if(auto r=w.u64(p.membership_generation);!r)return r;if(auto r=w.u64(p.permissions);!r)return r;
    return write8(w,p.revoked?1:0);
}
Result<PrincipalPolicy> read_policy(Reader& r) noexcept {
    std::array<std::uint64_t,6> v{};for(auto& n:v){auto x=r.u64();if(!x)return fail(Error::RecoveryUnavailable);n=*x;}
    auto k=r.raw(1);auto m=r.u64(),p=r.u64();auto revoked=r.raw(1);
    if(!k||!m||!p||!revoked||static_cast<unsigned>((*k)[0])>1||static_cast<unsigned>((*revoked)[0])>1)return fail(Error::RecoveryUnavailable);
    PrincipalPolicy out{v[0],v[1],v[2],v[3],v[4],v[5],static_cast<AuthorityKind>(static_cast<unsigned>((*k)[0])),*m,*p,(*revoked)[0]==std::byte{1}};
    if(!valid_policy(out))return fail(Error::RecoveryUnavailable);return out;
}
Status write_boot(Writer& w,const CoordinatorBootIdentity& boot) noexcept {if(auto r=w.u64(boot.term);!r)return r;return w.raw(boot.nonce);}
Result<CoordinatorBootIdentity> read_boot(Reader& r) noexcept {
    auto t=r.u64();auto n=r.raw(32);if(!t||!n)return fail(Error::RecoveryUnavailable);
    CoordinatorBootIdentity out{*t};std::copy(n->begin(),n->end(),out.nonce.begin());if(!nonzero_scope(out.nonce))return fail(Error::RecoveryUnavailable);return out;
}
struct PolicyRecord {PolicyReceipt receipt{};std::uint64_t confirmation{};};
Result<std::array<std::byte,policy_bytes>> encode_policy(const PolicyRecord& p) noexcept {
    std::array<std::byte,policy_bytes> bytes{};Writer w(bytes);const auto& c=p.receipt.change;
    if(!w.u64(c.transition_id)||!write8(w,c.expected_revision?1:0)||!w.u64(c.expected_revision.value_or(0))||!write_policy(w,c.policy)||
       !write_boot(w,c.boot)||!w.u64(p.receipt.revision)||!w.u64(p.confirmation)||w.size()!=bytes.size())return fail(Error::ProtocolViolation);return bytes;
}
Result<PolicyRecord> decode_policy(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()!=policy_bytes)return fail(Error::RecoveryUnavailable);Reader r(bytes);auto id=r.u64();auto presence=r.raw(1);auto expected=r.u64();
    auto policy=read_policy(r);auto boot=read_boot(r);auto revision=r.u64(),confirmation=r.u64();
    if(!id||!*id||!presence||!expected||!policy||!boot||!revision||!confirmation||!r.empty()||static_cast<unsigned>((*presence)[0])>1||
        ((*presence)[0]==std::byte{}&&*expected))return fail(Error::RecoveryUnavailable);
    if((*presence)[0]==std::byte{1}?(*expected==UINT64_MAX||*revision!=*expected+1):*revision!=0)return fail(Error::RecoveryUnavailable);
    PolicyRecord out;out.receipt.change={*id,{},*policy,*boot};if((*presence)[0]==std::byte{1})out.receipt.change.expected_revision=*expected;
    out.receipt.revision=*revision;out.confirmation=*confirmation;return out;
}
Result<PolicyRecord> policy_column(Statement& q,int column) noexcept {
    auto data=sqlite_blob(q.value,column,policy_bytes,policy_bytes);if(!data)return fail(data.error());
    return decode_policy(*data);
}
Status write_execution(Writer& w,const ExecutionLeaseFields& l) noexcept {
    if(!write_boot(w,l.issuer.boot)||!w.raw(l.issuer.clock_scope))return fail(Error::ProtocolViolation);const auto& g=l.grant;
    for(auto n:std::array<std::uint64_t,8>{g.request,g.coordinator_term,g.grant_sequence,g.membership_generation,g.epoch,g.owner,g.ttl_us,g.proof_generation})if(auto r=w.u64(n);!r)return r;
    if(!write8(w,static_cast<std::uint8_t>(g.kind))||!w.raw(g.scope))return fail(Error::ProtocolViolation);
    for(auto n:std::array<std::uint64_t,4>{l.anchor.continuous_us,l.anchor.active_us,l.anchor.sampling_uncertainty_us,l.deadline_us})if(auto r=w.u64(n);!r)return r;return {};
}
Result<std::array<std::byte,execution_bytes>> encode_execution(const ExecutionLeaseFields& l) noexcept {
    std::array<std::byte,execution_bytes> out{};Writer w(out);if(!write_execution(w,l)||w.size()!=out.size())return fail(Error::ProtocolViolation);return out;
}
Result<ExecutionLeaseFields> decode_execution(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()!=execution_bytes)return fail(Error::RecoveryUnavailable);Reader r(bytes);auto boot=read_boot(r);auto scope=r.raw(32);
    std::array<std::uint64_t,8> v{};for(auto& n:v){auto x=r.u64();if(!x)return fail(Error::RecoveryUnavailable);n=*x;}
    auto kind=r.raw(1),nonce=r.raw(32);std::array<std::uint64_t,4> c{};for(auto& n:c){auto x=r.u64();if(!x)return fail(Error::RecoveryUnavailable);n=*x;}
    if(!boot||!scope||!kind||!nonce||!r.empty()||static_cast<unsigned>((*kind)[0])>1||!v[4]||!v[6]||v[6]>2000000||c[2]>1000||c[3]<=c[0])return fail(Error::RecoveryUnavailable);
    ExecutionLeaseFields out;out.issuer.boot=*boot;std::copy(scope->begin(),scope->end(),out.issuer.clock_scope.begin());
    out.grant={v[0],v[1],v[2],v[3],v[4],v[5],static_cast<AuthorityKind>(static_cast<unsigned>((*kind)[0])),v[6],{},v[7]};
    std::copy(nonce->begin(),nonce->end(),out.grant.scope.begin());out.anchor={c[0],c[1],c[2]};out.deadline_us=c[3];
    if(!nonzero_scope(out.issuer.clock_scope)||!nonzero_scope(out.grant.scope)||out.grant.coordinator_term!=out.issuer.boot.term)return fail(Error::RecoveryUnavailable);return out;
}
struct StoredExecution {ExecutionLeaseFields fields{};std::uint64_t confirmation{};};
Result<StoredExecution> execution_state(sqlite3* db,std::uint64_t match) noexcept {
    if(auto r=table_exists(db,"control_leases");!r)return fail(r.error());Statement q;
    if(auto r=q.prepare(db,"SELECT data,confirmation FROM control_leases WHERE match=?1");!r)return fail(r.error());if(auto r=q.number(1,match);!r)return fail(r.error());
    auto code=sqlite3_step(q.value);if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto data=sqlite_blob(q.value,0,execution_bytes,execution_bytes);if(!data)return fail(data.error());
    auto out=decode_execution(*data);auto confirmation=q.number(1);
    if(!out)return fail(out.error());if(!confirmation)return fail(confirmation.error());
    if(sqlite3_step(q.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);return StoredExecution{*out,*confirmation};
}
Result<ExecutionLeaseFields> execution_row(sqlite3* db,std::uint64_t match) noexcept {
    auto stored=execution_state(db,match);if(!stored)return fail(stored.error());return stored->fields;
}
Status sample_matches(ClockSample now,ClockSample old,std::uint64_t deadline,std::uint64_t maximum_gap) noexcept {
    if(now.sampling_uncertainty_us>1000||old.sampling_uncertainty_us>1000||now.continuous_us<old.continuous_us||now.active_us<old.active_us)return fail(Error::RecoveryUnavailable);
    auto continuous=now.continuous_us-old.continuous_us,active=now.active_us-old.active_us;
    const auto difference=continuous>active?continuous-active:active-continuous;
    if(continuous>maximum_gap||difference>1000+now.sampling_uncertainty_us+old.sampling_uncertainty_us)return fail(Error::RecoveryUnavailable);
    if(now.continuous_us>=deadline||now.sampling_uncertainty_us>=deadline-now.continuous_us)return fail(Error::Timeout);return {};
}
// The committed identity remains immutable across lease renewals. This check
// runs only after control_validate has proved that the incoming permit matches
// the currently installed lease, authority, policy, boot and live clock, in the
// same BEGIN IMMEDIATE transaction as the canonical record comparison.
Status match_committed_binding(std::span<const std::byte> bytes,const ExecutionPermitFields& current) noexcept {
    if(bytes.size()!=binding_bytes)return fail(Error::RecoveryUnavailable);
    Reader r(bytes);auto boot=read_boot(r);auto clock_scope=r.raw(32);
    auto policy=read_policy(r);auto revision=r.u64();
    std::array<std::uint64_t,8> v{};
    for(auto& value:v){auto decoded=r.u64();if(!decoded)return fail(Error::RecoveryUnavailable);value=*decoded;}
    auto kind=r.raw(1),scope=r.raw(32);
    if(!boot||!clock_scope||!policy||!revision||!kind||!scope||!r.empty()||
       static_cast<unsigned>((*kind)[0])>1||!v[4]||!v[6]||v[6]>2000000)return fail(Error::RecoveryUnavailable);
    AuthorityGrant original{v[0],v[1],v[2],v[3],v[4],v[5],
        static_cast<AuthorityKind>(static_cast<unsigned>((*kind)[0])),v[6],{},v[7]};
    std::copy(scope->begin(),scope->end(),original.scope.begin());
    const auto& now=current.lease.grant;
    if(*boot!=current.lease.issuer.boot||
       !std::equal(clock_scope->begin(),clock_scope->end(),current.lease.issuer.clock_scope.begin())||
       *policy!=current.policy||*revision!=current.policy_revision||
       original.coordinator_term!=now.coordinator_term||original.membership_generation!=now.membership_generation||
       original.epoch!=now.epoch||original.owner!=now.owner||original.kind!=now.kind||
       original.scope!=now.scope||original.proof_generation!=now.proof_generation)return fail(Error::PermissionDenied);
    // An exact original grant remains an ordinary duplicate. Otherwise both
    // monotonic grant identifiers must advance; changing TTL alone is invalid.
    // The original binding is retained, never rewritten to the retry's grant.
    if(same_grant(original,now))return {};
    if(now.request<=original.request||now.grant_sequence<=original.grant_sequence)return fail(Error::PermissionDenied);
    return {};
}
Status control_reconfirm(sqlite3* db,std::uint64_t match,std::uint64_t id,const ExecutionPermit& permit,bool fresh) noexcept {
    const auto& p=permit.fields();std::array<std::byte,binding_bytes> binding{};Writer w(binding);
    if(!write_boot(w,p.lease.issuer.boot)||!w.raw(p.lease.issuer.clock_scope)||!write_policy(w,p.policy)||!w.u64(p.policy_revision))return fail(Error::ProtocolViolation);
    const auto& g=p.lease.grant;for(auto n:std::array<std::uint64_t,8>{g.request,g.coordinator_term,g.grant_sequence,g.membership_generation,g.epoch,g.owner,g.ttl_us,g.proof_generation})if(!w.u64(n))return fail(Error::ProtocolViolation);
    if(!write8(w,static_cast<std::uint8_t>(g.kind))||!w.raw(g.scope)||w.size()!=binding.size())return fail(Error::ProtocolViolation);
    std::uint64_t confirmation=0;
    if(fresh){if(auto r=execute(db,"CREATE TABLE IF NOT EXISTS control_commits(match BLOB NOT NULL,id BLOB NOT NULL,data BLOB NOT NULL,confirmation BLOB NOT NULL,PRIMARY KEY(match,id)) WITHOUT ROWID");!r)return r;}
    else{
        if(auto r=table_exists(db,"control_commits");!r)return fail(Error::PermissionDenied);Statement q;
        if(auto r=q.prepare(db,"SELECT data,confirmation FROM control_commits WHERE match=?1 AND id=?2");!r)return r;
        if(!q.number(1,match)||!q.number(2,id))return fail(Error::Io);auto code=sqlite3_step(q.value);if(code==SQLITE_DONE)return fail(Error::PermissionDenied);if(code!=SQLITE_ROW)return fail(sql_error(code));
        auto data=sqlite_blob(q.value,0,binding_bytes,binding_bytes);if(!data)return fail(data.error()==Error::OutOfMemory?Error::OutOfMemory:Error::PermissionDenied);
        if(auto matched=match_committed_binding(*data,p);!matched)return matched;
        auto count=q.number(1);if(!count)return fail(count.error());if(*count==UINT64_MAX)return fail(Error::CounterExhausted);confirmation=*count+1;
        if(sqlite3_step(q.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);
    }
    Statement write;if(auto r=write.prepare(db,"INSERT INTO control_commits VALUES(?1,?2,?3,?4) ON CONFLICT(match,id) DO UPDATE SET confirmation=excluded.confirmation");!r)return r;
    if(!write.number(1,match)||!write.number(2,id)||!write.bytes(3,binding)||!write.number(4,confirmation))return fail(Error::Io);
    auto code=sqlite3_step(write.value);return code==SQLITE_DONE?Status{}:Status(fail(sql_error(code)));
}
}

Status SqliteJournal::Impl::control_observe(bool restore) noexcept {
    if(!control_issuer||!control_clock)return fail(Error::NotReady);
    if(control_paused&&!restore)return fail(Error::RecoveryUnavailable);
    auto now=control_clock->sample();if(!now){control_paused=true;return fail(now.error());}
    if(now->sampling_uncertainty_us>1000){control_paused=true;return fail(Error::RecoveryUnavailable);}
    if(control_previous&&!restore){auto r=sample_matches(*now,*control_previous,UINT64_MAX,500000);if(!r){control_paused=true;return r;}}
    control_previous=*now;control_paused=false;return {};
}
Result<std::optional<service::control::PolicyReceipt>> SqliteJournal::Impl::control_policy(std::uint64_t session,std::uint64_t principal) noexcept {
    using namespace service::control;
    if(auto r=table_exists(database,"control_policies");!r){if(r.error()==Error::NotReady)return std::optional<PolicyReceipt>{};return fail(r.error());}
    Statement q;if(auto r=q.prepare(database,"SELECT data FROM control_policies WHERE match=?1 AND session=?2 AND principal=?3");!r)return fail(r.error());
    if(!q.number(1,config.match)||!q.number(2,session)||!q.number(3,principal))return fail(Error::Io);auto code=sqlite3_step(q.value);
    if(code==SQLITE_DONE)return std::optional<PolicyReceipt>{};if(code!=SQLITE_ROW)return fail(sql_error(code));auto record=policy_column(q,0);if(!record)return fail(record.error());
    if(record->receipt.change.policy.match!=config.match||record->receipt.change.policy.session!=session||record->receipt.change.policy.principal!=principal||sqlite3_step(q.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);
    return std::optional<PolicyReceipt>{record->receipt};
}
Status SqliteJournal::bind_control_issuer(const service::control::IssuerIdentity& identity) noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);const auto fields=identity.fields();
    if(!impl_->bound_boot||fields.boot!=*impl_->bound_boot)return fail(Error::StaleEpoch);
    if(auto r=impl_->check_boot();!r)return r;if(!nonzero_scope(fields.clock_scope))return fail(Error::InvalidArgument);
    if(impl_->control_issuer)return *impl_->control_issuer==fields?Status{}:Status(fail(Error::InvalidArgument));
    auto clock=PlatformClock::create();if(!clock)return fail(clock.error());auto sample=clock->sample();if(!sample)return fail(sample.error());
    if(sample->sampling_uncertainty_us>1000)return fail(Error::RecoveryUnavailable);
    impl_->control_clock.emplace(std::move(*clock));impl_->control_issuer=fields;impl_->control_previous=*sample;return {};
}
Status SqliteJournal::control_heartbeat() noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);
    return impl_->control_issuer?impl_->control_observe():Status{};
}
Result<std::optional<service::control::PolicyReceipt>> SqliteJournal::control_policy(std::uint64_t session,std::uint64_t principal) noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);if(!session||!principal)return fail(Error::InvalidArgument);return impl_->control_policy(session,principal);
}
Result<service::control::PolicyReceipt> SqliteJournal::commit_control_policy(const service::control::PolicyChange& input) noexcept {
    using namespace service::control;
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);const auto change=input;
    if(!change.transition_id||!valid_policy(change.policy)||change.policy.match!=impl_->config.match)return fail(Error::InvalidArgument);
    if(!impl_->bound_boot||change.boot!=*impl_->bound_boot)return fail(Error::StaleEpoch);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());if(auto r=impl_->check_boot();!r)return fail(r.error());
    auto current=impl_->control_policy(change.policy.session,change.policy.principal);if(!current)return fail(current.error());
    bool duplicate=false;std::uint64_t revision=0,confirmation=0;
    auto present=table_exists(impl_->database,"control_transitions");if(!present&&present.error()!=Error::NotReady)return fail(present.error());
    if(present){
        Statement prior;if(auto r=prior.prepare(impl_->database,"SELECT data FROM control_transitions WHERE match=?1 AND id=?2");!r)return fail(r.error());
        if(!prior.number(1,impl_->config.match)||!prior.number(2,change.transition_id))return fail(Error::Io);auto code=sqlite3_step(prior.value);
        if(code==SQLITE_ROW){auto old=policy_column(prior,0);if(!old)return fail(old.error());if(old->receipt.change!=change)return fail(Error::ProtocolViolation);
            if(old->confirmation==UINT64_MAX)return fail(Error::CounterExhausted);confirmation=old->confirmation+1;revision=old->receipt.revision;duplicate=true;
            if(sqlite3_step(prior.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);
        }else if(code!=SQLITE_DONE)return fail(sql_error(code));
    }
    if(!duplicate){
        if(change.expected_revision.has_value()!=static_cast<bool>(*current)||(*current&&*change.expected_revision!=(**current).revision))return fail(Error::StaleGeneration);
        if(*current){const auto& old=(**current).change.policy;if((**current).revision==UINT64_MAX)return fail(Error::CounterExhausted);revision=(**current).revision+1;
            if(change.policy.principal_epoch<old.principal_epoch||change.policy.authority_epoch<old.authority_epoch)return fail(Error::StaleEpoch);
            if(change.policy.principal_epoch==old.principal_epoch){auto expected=old;expected.revoked=change.policy.revoked;
                if(change.policy!=expected||old.revoked&&!change.policy.revoked)return fail(Error::StaleEpoch);
                if((**current).change.boot==change.boot&&old==change.policy)return fail(Error::InvalidArgument);
            }
        }
        if(auto r=execute(impl_->database,"CREATE TABLE IF NOT EXISTS control_policies(match BLOB NOT NULL,session BLOB NOT NULL,principal BLOB NOT NULL,data BLOB NOT NULL,PRIMARY KEY(match,session,principal)) WITHOUT ROWID;CREATE TABLE IF NOT EXISTS control_transitions(match BLOB NOT NULL,id BLOB NOT NULL,data BLOB NOT NULL,PRIMARY KEY(match,id)) WITHOUT ROWID");!r)return fail(r.error());
        auto count=impl_->totals("SELECT COUNT(*),0 FROM control_transitions");if(!count)return fail(count.error());if((*count)[0]>=1024)return fail(Error::CapacityExceeded);
        if(!*current){auto principals=impl_->totals("SELECT COUNT(*),0 FROM control_policies");if(!principals)return fail(principals.error());if((*principals)[0]>=128)return fail(Error::CapacityExceeded);}
    }
    PolicyRecord record{{change,revision,duplicate,true},confirmation};auto encoded=encode_policy(record);if(!encoded)return fail(encoded.error());
    bool effective=!duplicate||(*current&&(**current).revision==revision&&(**current).change==change);
    if(effective){Statement write;if(auto r=write.prepare(impl_->database,"INSERT INTO control_policies VALUES(?1,?2,?3,?4) ON CONFLICT(match,session,principal) DO UPDATE SET data=excluded.data");!r)return fail(r.error());
        if(!write.number(1,impl_->config.match)||!write.number(2,change.policy.session)||!write.number(3,change.policy.principal)||!write.bytes(4,*encoded))return fail(Error::Io);
        auto code=sqlite3_step(write.value);if(code!=SQLITE_DONE)return fail(sql_error(code));}
    Statement history;if(auto r=history.prepare(impl_->database,"INSERT INTO control_transitions VALUES(?1,?2,?3) ON CONFLICT(match,id) DO UPDATE SET data=excluded.data");!r)return fail(r.error());
    if(!history.number(1,impl_->config.match)||!history.number(2,change.transition_id)||!history.bytes(3,*encoded))return fail(Error::Io);
    auto code=sqlite3_step(history.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    if(auto r=impl_->check_boot();!r)return fail(r.error());if(auto r=tx.commit(impl_->config.match);!r)return fail(r.error());record.receipt.current=effective;return record.receipt;
}
Status SqliteJournal::install_execution_lease(const service::control::ExecutionLease& execution) noexcept {
    using namespace service::control;
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);const auto lease=execution.fields();
    if(!impl_->control_issuer||lease.issuer!=*impl_->control_issuer)return fail(Error::StaleGeneration);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return r;if(auto r=impl_->check_boot();!r)return r;
    auto authority=impl_->authority_state();auto prefix=impl_->prefix();if(!authority||!*authority||!prefix)return fail(Error::NotReady);
    if((**authority).receipt.boot!=lease.issuer.boot||!same_grant((**authority).receipt.proposal.grant,lease.grant)||prefix->epoch!=lease.grant.epoch)return fail(Error::StaleEpoch);
    auto now=impl_->control_clock->sample();if(!now)return fail(now.error());if(auto r=sample_matches(*now,lease.anchor,lease.deadline_us,500000);!r)return r;
    auto old=execution_state(impl_->database,impl_->config.match);
    if(old&&old->fields.issuer==lease.issuer&&same_grant(old->fields.grant,lease.grant)&&lease.deadline_us>old->fields.deadline_us)return fail(Error::ProtocolViolation);
    if(!old&&old.error()!=Error::NotReady)return fail(old.error());
    if(old&&old->confirmation==UINT64_MAX)return fail(Error::CounterExhausted);
    const auto confirmation=old?old->confirmation+1:0;
    auto encoded=encode_execution(lease);if(!encoded)return fail(encoded.error());
    if(auto r=execute(impl_->database,"CREATE TABLE IF NOT EXISTS control_leases(match BLOB PRIMARY KEY,data BLOB NOT NULL,confirmation BLOB NOT NULL CHECK(length(confirmation)=8)) WITHOUT ROWID");!r)return r;
    Statement write;if(auto r=write.prepare(impl_->database,"INSERT INTO control_leases VALUES(?1,?2,?3) ON CONFLICT(match) DO UPDATE SET data=excluded.data,confirmation=excluded.confirmation");!r)return r;
    if(!write.number(1,impl_->config.match)||!write.bytes(2,*encoded)||!write.number(3,confirmation))return fail(Error::Io);auto code=sqlite3_step(write.value);if(code!=SQLITE_DONE)return fail(sql_error(code));
    auto final_sample=impl_->control_clock->sample();if(!final_sample)return fail(final_sample.error());if(auto r=sample_matches(*final_sample,lease.anchor,lease.deadline_us,500000);!r)return r;
    if(auto r=impl_->check_boot();!r)return r;if(auto r=tx.commit(impl_->config.match);!r)return r;impl_->control_previous=*final_sample;impl_->control_paused=false;return {};
}
Status SqliteJournal::Impl::control_validate(const service::control::ExecutionPermit& permit) noexcept {
    using namespace service::control;const auto& p=permit.fields();
    if(!control_issuer||p.lease.issuer!=*control_issuer||!bound_boot||p.lease.issuer.boot!=*bound_boot)return fail(Error::StaleEpoch);
    if(auto r=check_boot();!r)return r;if(auto r=control_observe();!r)return r;
    if(p.deadline_us>p.lease.deadline_us||!p.connection_incarnation||(p.permission!=2&&p.permission!=4))return fail(Error::PermissionDenied);
    auto lease=execution_row(database,config.match);if(!lease)return fail(lease.error());if(!same_execution_lease(*lease,p.lease))return fail(Error::StaleGeneration);
    if(auto r=sample_matches(*control_previous,p.lease.anchor,p.deadline_us,2000000);!r)return r;
    auto authority=authority_state();auto prefix_value=prefix();if(!authority||!*authority||!prefix_value)return fail(Error::RecoveryUnavailable);
    if((**authority).receipt.boot!=p.lease.issuer.boot||!same_grant((**authority).receipt.proposal.grant,p.lease.grant)||prefix_value->epoch!=p.policy.authority_epoch)return fail(Error::StaleEpoch);
    auto policy=control_policy(p.policy.session,p.policy.principal);if(!policy)return fail(policy.error());
    if(!*policy||(**policy).change.boot!=p.lease.issuer.boot||(**policy).revision!=p.policy_revision||(**policy).change.policy!=p.policy||p.policy.revoked||!(p.policy.permissions&p.permission))return fail(Error::PermissionDenied);
    const auto& g=p.lease.grant;
    if(p.policy.match!=config.match||p.policy.authority_epoch!=g.epoch||p.policy.authority_owner!=g.owner||p.policy.authority_kind!=g.kind||p.policy.membership_generation!=g.membership_generation)return fail(Error::PermissionDenied);
    return {};
}
Result<JournalPrefix> SqliteJournal::read_control(const service::control::ExecutionPermit& permit) noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);if(permit.fields().permission!=2)return fail(Error::PermissionDenied);
    Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());if(auto r=impl_->control_validate(permit);!r)return fail(r.error());
    auto prefix=impl_->prefix();if(!prefix)return fail(prefix.error());
    if(auto r=impl_->reconfirm();!r)return fail(r.error());
    if(auto r=impl_->control_validate(permit);!r)return fail(r.error());
    if(auto r=tx.commit();!r)return fail(r.error());return prefix;
}
Result<JournalReceipt> SqliteJournal::append_control(const service::control::ExecutionPermit& permit,std::uint64_t id,Tick tick,std::span<const std::byte> data) noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own_thread())return fail(Error::PermissionDenied);
    if(permit.fields().permission!=4)return fail(Error::PermissionDenied);
    if(!id||data.size()>4000||data.size()>impl_->config.maximum_record_bytes)return fail(Error::InvalidArgument);
    return impl_->append_transaction(permit.fields().policy.authority_epoch,id,tick,data,{},{},{},&permit);
}
