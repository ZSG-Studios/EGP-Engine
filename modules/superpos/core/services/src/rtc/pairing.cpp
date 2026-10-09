// SPDX-License-Identifier: MIT
#include "superpos/service/rtc/pairing.hpp"
#include "executor_identity.hpp"
#include <algorithm>
#include <cstdlib>
#include <new>

namespace superpos::service::pairing {

namespace {
std::atomic<std::uint64_t> registry_ids{1};
struct Enter {
    bool& busy; explicit Enter(bool& value) noexcept:busy(value){busy=true;}
    ~Enter(){busy=false;}
};
bool same(const admission::Grant& a,const admission::Grant& b) noexcept {
    return a.match==b.match&&a.session==b.session&&a.authority_epoch==b.authority_epoch&&
        a.connection_incarnation==b.connection_incarnation&&a.actor==b.actor&&a.actor_epoch==b.actor_epoch&&a.permissions==b.permissions;
}
bool valid(const Scope& scope,const Certificates& certificates) noexcept {
    const auto& p=scope.principal;
    return p.match&&p.session&&p.connection_incarnation&&p.actor&&(p.permissions&1)&&!(p.permissions&~admission::known_permissions)&&
        known_fingerprint(certificates.local_control)&&known_fingerprint(certificates.remote_control)&&
        known_fingerprint(certificates.local_state)&&known_fingerprint(certificates.remote_state);
}
bool role(CarrierLane value) noexcept {return value==CarrierLane::Control||value==CarrierLane::State;}
// Fixed canonical little-endian integers and raw SHA256 certificate bytes.
// One pair-wide MAC commits both explicit role tags and both certificate pairs.
// Each role has a separate consumed bit; it is not a distinct per-role MAC.
constexpr std::size_t transcript_size=transcript_domain.size()+16*8+5*32;
std::array<std::byte,transcript_size> transcript(const Offer& o) noexcept {
    std::array<std::byte,transcript_size> bytes{};std::size_t at{};
    auto u64=[&](std::uint64_t value){for(unsigned i=0;i<8;++i)bytes[at++]=std::byte(value>>(i*8));};
    auto raw=[&](const auto& value){for(auto b:value)bytes[at++]=b;};
    for(char c:transcript_domain)bytes[at++]=std::byte(c);
    u64(1);u64(o.pair.registry);u64(o.pair.generation);
    const auto& p=o.scope.principal;
    u64(p.match);u64(p.session);u64(p.authority_epoch);u64(p.connection_incarnation);
    u64(p.actor);u64(p.actor_epoch);u64(p.permissions);u64(o.scope.peer);
    u64(o.issued_us);u64(o.expires_us);u64(o.continuity_generation);raw(o.nonce);
    u64(static_cast<std::uint64_t>(CarrierLane::Control));raw(o.certificates.local_control);raw(o.certificates.remote_control);
    u64(static_cast<std::uint64_t>(CarrierLane::State));raw(o.certificates.local_state);raw(o.certificates.remote_state);
    if(at!=bytes.size())std::abort();
    return bytes;
}
}
struct Registry::Pool {
    struct Row {
        Offer offer{};
        std::uint64_t associations[2]{};
        bool occupied{},used[2]{},active{},closing{};
    };
    struct Connection {Binding binding{};bool occupied{},closing{};};
    std::array<Row,pending_capacity> rows{};
    std::array<Connection,connected_capacity> connections{};
    std::size_t active{},connected{};
};
Status Registry::own() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);
    if(!pool_)return fail(Error::NotReady);
    return {};
}
Registry::~Registry(){
    if(owner_!=std::this_thread::get_id()||busy_)std::abort();
    busy_=true;
    if(pool_){
        for(const auto& row:pool_->rows)if(row.occupied)
            for(auto id:row.associations)if(id&&!native_.close_and_quiesce(id))std::abort();
        for(const auto& row:pool_->connections)if(row.occupied){
            if(!native_.close_and_quiesce(row.binding.control_association)||
               !native_.close_and_quiesce(row.binding.state_association))std::abort();
        }
    }
    auto* dying=pool_;pool_=nullptr;
    if(dying){dying->~Pool();allocator_.deallocate(dying);}
}
Status Registry::initialize() noexcept {
    static_assert(sizeof(Pool) <= 256 * 1024);
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(busy_||pool_)return fail(Error::Busy);
    Enter enter(busy_);
    if(digest_.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    auto id=detail::reserve_executor_identity(registry_ids);if(!id)return fail(id.error());
    void* memory=allocator_.allocate(sizeof(Pool),alignof(Pool),MemoryDomain::Backend);
    if(!memory)return fail(Error::OutOfMemory);
    pool_=new(memory)Pool;incarnation_=*id;return {};
}
void Registry::remove(std::size_t index) noexcept {
    auto& row=pool_->rows[index];if(row.active)--pool_->active;row=Pool::Row{};
}
Status Registry::retire(std::size_t index) noexcept {
    auto& row=pool_->rows[index];row.closing=true;Error pending=Error::None;
    for(auto id:row.associations)if(id){auto stopped=native_.close_and_quiesce(id);if(!stopped&&pending==Error::None)pending=stopped.error();}
    if(pending!=Error::None)return fail(pending);
    remove(index);return {};
}
bool Registry::claimed(std::uint64_t id) const noexcept {
    for(const auto& row:pool_->rows)if(row.occupied)for(auto owned:row.associations)if(owned==id)return true;
    for(const auto& row:pool_->connections)if(row.occupied&&
        (row.binding.control_association==id||row.binding.state_association==id))return true;
    return false;
}
Result<ClockObservation> Registry::observe() noexcept {
    auto value=continuity_.observe(clock_);
    if(!value){for(std::size_t i=0;i<pending_capacity;++i)if(pool_->rows[i].occupied)(void)retire(i);return fail(value.error());}
    return value;
}
Result<std::size_t> Registry::find(Token token) const noexcept {
    if(token.registry!=incarnation_||!token.generation)return fail(Error::StaleGeneration);
    for(std::size_t i=0;i<pending_capacity;++i)if(pool_->rows[i].occupied&&pool_->rows[i].offer.pair==token)return i;
    return fail(Error::StaleGeneration);
}
Result<Offer> Registry::issue(const Scope& input,const Certificates& certs) noexcept {
    if(auto status=own();!status)return fail(status.error());
    const Scope scope=input;const Certificates certificates=certs;
    if(!valid(scope,certificates))return fail(Error::InvalidArgument);
    Enter enter(busy_);if(retired_)return fail(Error::CounterExhausted);
    auto now=observe();if(!now)return fail(now.error());
    std::size_t free=pending_capacity;
    for(std::size_t i=0;i<pending_capacity;++i)if(!pool_->rows[i].occupied){free=i;break;}
    if(free==pending_capacity)return fail(Error::CapacityExceeded);
    if(now->now_us>UINT64_MAX-deadline_us){retired_=true;return fail(Error::CounterExhausted);}
    auto generation=detail::reserve_executor_identity(next_);
    if(!generation){retired_=true;return fail(generation.error());}
    Offer offer{};offer.pair={incarnation_,*generation};offer.scope=scope;offer.certificates=certificates;
    offer.issued_us=now->now_us;offer.expires_us=now->now_us+deadline_us;offer.continuity_generation=now->proof_generation;
    if(auto allowed=authorization_.check(scope);!allowed)return fail(allowed.error());
    if(auto fresh=nonces_.fresh(offer.nonce);!fresh)return fail(fresh.error());
    if(!known_fingerprint(offer.nonce))return fail(Error::AuthenticationFailed);
    for(const auto& row:pool_->rows)if(row.occupied&&row.offer.nonce==offer.nonce)return fail(Error::AuthenticationFailed);
    Fingerprint commitment{};const auto bytes=transcript(offer);
    if(auto hashed=digest_.hash(bytes,commitment);!hashed)return fail(hashed.error());
    if(!known_fingerprint(commitment))return fail(Error::AuthenticationFailed);
    const auto& p=scope.principal;
    offer.request={{p.match,p.session,p.authority_epoch,p.connection_incarnation,p.permissions,commitment},p.actor,p.actor_epoch,p.permissions,{}};
    offer.control={offer.pair,CarrierLane::Control,offer.nonce};offer.state={offer.pair,CarrierLane::State,offer.nonce};
    if(auto allowed=authorization_.check(scope);!allowed)return fail(allowed.error());
    auto after=observe();if(!after)return fail(after.error());
    if(after->proof_generation!=offer.continuity_generation)return fail(Error::StaleGeneration);
    if(after->now_us>=offer.expires_us)return fail(Error::Timeout);
    // All callbacks completed. Publish the complete immutable offer at once.
    auto& row=pool_->rows[free];row.offer=offer;row.occupied=true;return offer;
}
Result<Started> Registry::begin(Token token) noexcept {
    if(auto status=own();!status)return fail(status.error());
    auto found=find(token);if(!found)return fail(found.error());Enter enter(busy_);
    auto& row=pool_->rows[*found];if(row.closing)return fail(Error::NotReady);if(row.active)return fail(Error::Busy);
    if(pool_->active==active_capacity)return fail(Error::CapacityExceeded);
    auto reject=[&](Error error)->Result<Started>{(void)retire(*found);return fail(error);};
    const Scope scope=row.offer.scope;
    if(auto allowed=authorization_.check(scope);!allowed)return reject(allowed.error());
    auto now=observe();if(!now)return fail(now.error());
    if(now->proof_generation!=row.offer.continuity_generation)return reject(Error::StaleGeneration);
    if(now->now_us>=row.offer.expires_us)return reject(Error::Timeout);
    row.active=true;++pool_->active; // Before either native create callback.
    const Offer offer=row.offer;
    for(unsigned index=0;index<2;++index){
        auto created=native_.create(offer,index?CarrierLane::State:CarrierLane::Control);
        if(!created)return reject(created.error());
        if(!*created||claimed(*created))return reject(Error::ProtocolViolation);
        row.associations[index]=*created;
        if(auto allowed=authorization_.check(scope);!allowed)return reject(allowed.error());
        auto current=observe();if(!current)return fail(current.error());
        if(current->proof_generation!=offer.continuity_generation)return reject(Error::StaleGeneration);
        if(current->now_us>=offer.expires_us)return reject(Error::Timeout);
    }
    return Started{token,row.associations[0],row.associations[1]};
}
Status Registry::admit(const Ticket& input,const admission::Request& proof,const ObservedAssociation& observed) noexcept {
    if(auto status=own();!status)return status;
    const Ticket ticket=input;const admission::Request request=proof;const ObservedAssociation association=observed;
    if(!role(ticket.role)||ticket.role!=association.role||!association.identity)return fail(Error::InvalidArgument);
    auto found=find(ticket.pair);if(!found)return fail(found.error());
    Enter enter(busy_);auto& row=pool_->rows[*found];const auto index=ticket.role==CarrierLane::Control?0u:1u;
    if(row.closing||!row.active)return fail(Error::NotReady);
    if(ticket.nonce!=row.offer.nonce)return fail(Error::AuthenticationFailed);
    if(row.used[index])return fail(Error::StaleGeneration);
    auto reject=[&](Error error)->Status{(void)retire(*found);return fail(error);};
    auto now=observe();if(!now)return fail(now.error());
    if(now->proof_generation!=row.offer.continuity_generation)return reject(Error::StaleGeneration);
    if(now->now_us>=row.offer.expires_us)return reject(Error::Timeout);
    const auto& certificates=row.offer.certificates;
    if(association.local!=(index?certificates.local_state:certificates.local_control)||
       association.remote!=(index?certificates.remote_state:certificates.remote_control))return reject(Error::AuthenticationFailed);
    if(association.identity!=row.associations[index])return reject(Error::AuthenticationFailed);
    if(row.associations[1-index]==association.identity)return reject(Error::ProtocolViolation);
    // Native identities were reserved by begin; consume before crypto callbacks.
    row.used[index]=true;
    const auto expected=row.offer.scope.principal;
    if(request.actor!=expected.actor||request.actor_epoch!=expected.actor_epoch||request.requested_permissions!=expected.permissions)
        return reject(Error::AuthenticationFailed);
    const auto challenge=row.offer.request.challenge;
    if(auto matching=admission::matches_issued(request,challenge);!matching)return reject(matching.error());
    auto verified=verifier_.verify(request,challenge);
    if(!verified)return reject(verified.error());
    if(!same(*verified,expected))return reject(Error::PermissionDenied);
    const Scope scope=row.offer.scope;
    if(auto allowed=authorization_.check(scope);!allowed)return reject(allowed.error());
    auto after=observe();if(!after)return fail(after.error());
    if(after->proof_generation!=row.offer.continuity_generation)return reject(Error::StaleGeneration);
    if(after->now_us>=row.offer.expires_us)return reject(Error::Timeout);
    return {};
}
Result<Binding> Registry::take(Token token) noexcept {
    if(auto status=own();!status)return fail(status.error());
    auto found=find(token);if(!found)return fail(found.error());Enter enter(busy_);
    auto& row=pool_->rows[*found];
    if(row.closing)return fail(Error::NotReady);
    if(!row.used[0]||!row.used[1])return fail(Error::Busy);
    std::size_t destination=connected_capacity;
    for(std::size_t i=0;i<connected_capacity;++i)if(!pool_->connections[i].occupied){destination=i;break;}
    if(destination==connected_capacity)return fail(Error::CapacityExceeded);
    const Scope scope=row.offer.scope;
    if(auto allowed=authorization_.check(scope);!allowed){(void)retire(*found);return fail(allowed.error());}
    auto now=observe();if(!now)return fail(now.error());
    if(now->proof_generation!=row.offer.continuity_generation){(void)retire(*found);return fail(Error::StaleGeneration);}
    if(now->now_us>=row.offer.expires_us){(void)retire(*found);return fail(Error::Timeout);}
    Binding result{row.offer,row.associations[0],row.associations[1],OwnershipToken(token)};
    pool_->connections[destination]={result,true,false};++pool_->connected;
    row.associations[0]=row.associations[1]=0;remove(*found);return result;
}
Result<Binding> Registry::binding(OwnershipToken token) const noexcept {
    if(auto status=own();!status)return fail(status.error());
    for(const auto& row:pool_->connections)if(row.occupied&&row.binding.ownership==token){
        if(row.closing)return fail(Error::NotReady);return row.binding;
    }
    return fail(Error::StaleGeneration);
}
Result<Binding> Registry::checked_binding(OwnershipToken token) noexcept {
    if(auto status=own();!status)return fail(status.error());Enter enter(busy_);
    for(auto& row:pool_->connections)if(row.occupied&&row.binding.ownership==token){
        if(row.closing)return fail(Error::NotReady);
        auto reject=[&](Error error)->Result<Binding>{
            if(error!=Error::Busy)row.closing=true;
            return fail(error);
        };
        const Binding captured=row.binding;
        auto before=continuity_.observe(clock_);if(!before)return reject(before.error());
        if(before->proof_generation!=captured.offer.continuity_generation)return reject(Error::StaleGeneration);
        if(auto allowed=authorization_.check(captured.offer.scope);!allowed)return reject(allowed.error());
        struct Capture final:ClockSource {
            ClockSource& source;ClockSample sampled{};
            explicit Capture(ClockSource& value)noexcept:source(value){}
            Result<ClockSample> sample()noexcept override {
                auto value=source.sample();if(value)sampled=*value;return value;
            }
        } captured_clock(clock_);
        auto after=continuity_.observe(captured_clock);if(!after)return reject(after.error());
        if(after->proof_generation!=captured.offer.continuity_generation)return reject(Error::StaleGeneration);
        // The clock observation itself is a trusted callback. Recheck policy
        // afterward rather than returning its earlier admission result.
        if(auto allowed=authorization_.check(captured.offer.scope);!allowed)return reject(allowed.error());
        auto final=continuity_.observe(captured_clock.sampled);if(!final)return reject(final.error());
        if(final->proof_generation!=captured.offer.continuity_generation)return reject(Error::StaleGeneration);
        return captured;
    }
    return fail(Error::StaleGeneration);
}
Status Registry::release(OwnershipToken token) noexcept {
    if(auto status=own();!status)return status;Enter enter(busy_);
    for(auto& row:pool_->connections)if(row.occupied&&row.binding.ownership==token){
        row.closing=true;Error pending=Error::None;
        for(auto id:{row.binding.control_association,row.binding.state_association}){
            auto closed=native_.close_and_quiesce(id);if(!closed&&pending==Error::None)pending=closed.error();
        }
        if(pending!=Error::None)return fail(pending);
        row=Pool::Connection{};--pool_->connected;return {};
    }
    return fail(Error::StaleGeneration);
}
Status Registry::cancel(Token token) noexcept {
    if(auto status=own();!status)return status;
    auto found=find(token);if(!found)return fail(found.error());Enter enter(busy_);return retire(*found);
}
Status Registry::sweep() noexcept {
    if(auto status=own();!status)return status;Enter enter(busy_);auto now=observe();if(!now)return fail(now.error());
    Error pending=Error::None;
    for(std::size_t i=0;i<pending_capacity;++i){const auto& row=pool_->rows[i];
        if(row.occupied&&(row.closing||row.offer.continuity_generation!=now->proof_generation||now->now_us>=row.offer.expires_us)){
            auto closed=retire(i);if(!closed&&pending==Error::None)pending=closed.error();
        }}
    return pending==Error::None?Status{}:Status(fail(pending));
}
Result<Counts> Registry::counts() const noexcept {
    if(auto status=own();!status)return fail(status.error());std::size_t pending{};
    for(const auto& row:pool_->rows)pending+=row.occupied;
    return Counts{pending,pool_->active,pool_->connected,sizeof(Pool)};
}
}
