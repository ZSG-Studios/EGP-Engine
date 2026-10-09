// SPDX-License-Identifier: MIT
#include "rtc_owner.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace superpos_egp {
using namespace superpos;
namespace {
struct Enter { bool& flag; Enter(bool& f):flag(f){flag=true;} ~Enter(){flag=false;} };
service::admission::CredentialProvider& required(
        const AllocatedOwner<service::admission::CredentialProvider>& p) noexcept {
    if(!p)std::abort();return *p;
}
}
RtcOwner::RtcOwner(BudgetAllocator& parent, AllocatedOwner<service::admission::CredentialProvider> c,
                   superpos::AllocatedOwner<AttachmentPolicy> a, MemoryPlan peer_plan) noexcept:
    allocator_(parent),peer_plan_(peer_plan),credentials_(std::move(c)),authorization_(std::move(a)),crypto_(required(credentials_)) {
    if(!credentials_||!authorization_)std::abort();
}
RtcOwner::~RtcOwner(){ if(std::this_thread::get_id()!=thread_||busy_||state_!=State::Stopped)std::abort(); }
Status RtcOwner::own() const noexcept {
    if(std::this_thread::get_id()!=thread_)return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);
    return {};
}
Status RtcOwner::initialize(pairing::NativeIceConfig config) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Fresh)return fail(Error::NotReady);
    Enter lock(busy_);
    if(credentials_.backing_allocator()!=&allocator_||authorization_.backing_allocator()!=&allocator_){state_=State::Stopping;return fail(Error::InvalidArgument);}
    auto parent_status=allocator_.configuration_status();
    if(!parent_status){state_=State::Stopping;return parent_status;}
    for(auto& row:connections_){
        auto bound=row.allocator.bind(allocator_,peer_plan_);
        if(!bound){state_=State::Stopping;return bound;}
    }
    auto clock=PlatformClock::create();
    if(!clock){state_=State::Stopping;return fail(clock.error());}
    clock_.emplace(std::move(*clock));
    native_.emplace(allocator_,*clock_,continuity_);
    auto initialized=native_->initialize(config);
    if(!initialized){state_=State::Stopping;return initialized;}
    registry_.emplace(allocator_,*clock_,continuity_,crypto_,crypto_,crypto_,*authorization_,*native_);
    auto registry=registry_->initialize();
    if(!registry){state_=State::Stopping;return registry;}
    state_=State::Running;return {};
}
Status RtcOwner::revalidate(std::uint64_t generation,std::uint64_t uncertainty) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);auto sampled=clock_->sample();if(!sampled)return fail(sampled.error());
    return continuity_.revalidate(*sampled,generation,uncertainty);
}
Result<pairing::Offer> RtcOwner::issue(const pairing::Scope& scope,const pairing::Certificates& certs) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);
    auto revision=authorization_->policy_revision();
    if(!revision)return fail(revision.error());
    for(auto& row:pending_)if(!row.occupied){
        auto offer=registry_->issue(scope,certs);if(!offer)return fail(offer.error());
        row.token=offer->pair;row.offer=*offer;row.occupied=true;
        auto after=authorization_->policy_revision();
        if(!after||*after!=*revision){
            row.closing=true; // Retain the successful Registry issue until drain.
            return fail(after?Error::StaleGeneration:after.error());
        }
        return *offer;
    }
    return fail(Error::CapacityExceeded);
}
RtcOwner::Pending* RtcOwner::pending(pairing::Token token) noexcept {
    for(auto& row:pending_)if(row.occupied&&!row.closing&&row.token==token)return &row;
    return nullptr;
}
Result<std::uint64_t> RtcOwner::association(pairing::Token token,CarrierLane lane) noexcept {
    if(lane!=CarrierLane::Control&&lane!=CarrierLane::State)return fail(Error::InvalidArgument);
    auto* row=pending(token);if(!row)return fail(Error::StaleGeneration);
    if(!row->started)return fail(Error::NotReady);
    return lane==CarrierLane::Control?row->associations.control_association:row->associations.state_association;
}
Result<Fingerprint> RtcOwner::local_fingerprint() noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);return native_->local_fingerprint();
}
Status RtcOwner::start(pairing::Token token,pairing::Negotiation mode) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running)return fail(Error::NotReady);
    if(mode!=pairing::Negotiation::Offerer&&mode!=pairing::Negotiation::Answerer)return fail(Error::InvalidArgument);
    Enter lock(busy_);auto* row=pending(token);if(!row)return fail(Error::StaleGeneration);
    if(row->started)return fail(Error::Busy);
    auto begun=registry_->begin(token);
    if(!begun){row->closing=true;return fail(begun.error());}
    row->associations=*begun;row->started=true;
    for(auto id:{begun->control_association,begun->state_association}){
        auto started=native_->start(id,mode);if(!started){row->closing=true;return started;}
    }
    return {};
}
Status RtcOwner::submit_remote(pairing::Token token,CarrierLane lane,pairing::Negotiation sender,std::span<const char> text) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running)return fail(Error::NotReady);
    if(text.empty()||text.size()>pairing::signaling_bytes||!text.data())return fail(Error::InvalidArgument);
    // Snapshot caller storage before entering the native backend. No remote
    // text or pointer is retained by the owner-facing API.
    std::array<char,pairing::signaling_bytes> captured{};
    std::copy(text.begin(),text.end(),captured.begin());
    Enter lock(busy_);auto id=association(token,lane);if(!id)return fail(id.error());
    return native_->submit_remote(*id,sender,std::span(captured).first(text.size()));
}
Result<pairing::DescriptionText> RtcOwner::local_description(pairing::Token token,CarrierLane lane) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);auto id=association(token,lane);if(!id)return fail(id.error());
    return native_->local_description(*id);
}
Result<pairing::NativeInfo> RtcOwner::native_info(pairing::Token token,CarrierLane lane) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);auto id=association(token,lane);if(!id)return fail(id.error());
    return native_->info(*id);
}
Status RtcOwner::admit(pairing::Token token,CarrierLane lane,const service::admission::Request& input) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running)return fail(Error::NotReady);
    const auto request=input;
    Enter lock(busy_);auto id=association(token,lane);if(!id)return fail(id.error());
    auto observed=native_->observed(*id);if(!observed)return fail(observed.error());
    auto* row=pending(token);
    const auto ticket=lane==CarrierLane::Control?row->offer.control:row->offer.state;
    auto accepted=registry_->admit(ticket,request,*observed);
    if(!accepted)row->closing=true;
    return accepted;
}
Result<SessionConfig> RtcOwner::prepare_session(pairing::Token token,SessionConfig captured) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    if(auto valid=validate_capability_manifest(captured.capabilities);!valid)return fail(valid.error());
    Enter lock(busy_);
    auto revision=authorization_->policy_revision();
    if(!revision)return fail(revision.error());
    auto* row=pending(token);if(!row)return fail(Error::StaleGeneration);
    const auto scope=row->offer.scope;
    if(auto allowed=authorization_->check(scope);!allowed)return fail(allowed.error());
    auto mapped=authorization_->session_binding(scope);if(!mapped)return fail(mapped.error());
    if(!scope.principal.session||!mapped->connection_epoch||mapped->local_peer==scope.peer)
        return fail(Error::PermissionDenied);
    if(auto allowed=authorization_->check(scope);!allowed)return fail(allowed.error());
    auto after=authorization_->policy_revision();
    if(!after)return fail(after.error());
    if(*after!=*revision)return fail(Error::StaleGeneration);
    captured.session_id=scope.principal.session;
    captured.epoch=mapped->connection_epoch;
    captured.local_peer=mapped->local_peer;
    captured.remote_peer=scope.peer;
    // attach revalidates the same policy after constructing the Session.
    return captured;
}
Result<RtcOwner::Handle> RtcOwner::attach(pairing::Token token,const SessionConfig& config) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    // SessionConfig, DeliveryLimits, CapabilityManifest and AdmissionRequirements
    // currently contain only scalar/enumeration/inline-array values. No span,
    // string_view, pointer or other nested borrowed storage survives this copy.
    // Snapshot before take/activate/create invoke any trusted callback.
    const SessionConfig captured=config;
    if(auto valid=validate_capability_manifest(captured.capabilities);!valid)return fail(valid.error());
    Enter lock(busy_);
    auto revision=authorization_->policy_revision();
    if(!revision)return fail(revision.error());
    Pending* pending=nullptr;
    for(auto& p:pending_)if(p.occupied&&p.token==token&&!p.closing)pending=&p;
    if(!pending)return fail(Error::StaleGeneration);
    if(generation_==std::numeric_limits<std::uint64_t>::max())return fail(Error::CounterExhausted);
    for(std::size_t i=0;i<connections_.size();++i){auto& row=connections_[i];if(row.occupied)continue;
        auto binding=registry_->take(token);if(!binding)return fail(binding.error());
        // From successful take onward, every exit leaves the claim ledgered.
        row.occupied=true;row.claim=binding->ownership;row.generation=++generation_;*pending={};
        auto reject=[&](Error error)->Result<Handle>{detach(row);return fail(error);};
        const auto scope=binding->offer.scope;
        auto mapped=authorization_->session_binding(scope);
        if(!mapped)return reject(mapped.error());
        if(!mapped->connection_epoch||captured.session_id!=scope.principal.session||
           captured.remote_peer!=scope.peer||captured.local_peer!=mapped->local_peer||
           captured.local_peer==captured.remote_peer||captured.epoch!=mapped->connection_epoch)
            return reject(Error::PermissionDenied);
        row.scope=scope;row.binding=*mapped;
        row.continuity_generation=binding->offer.continuity_generation;
        auto active=native_->activate(*registry_,row.claim);if(!active)return reject(active.error());
        auto carrier=pairing::PairedTransport::create(row.allocator,*registry_,*native_,row.claim);
        if(!carrier)return reject(carrier.error());row.carrier.emplace(std::move(*carrier));
        // SCTP supplies congestion/receipts; PacketTransport is prohibited here.
        auto session=Session::create(row.allocator,*row.carrier,captured);
        if(!session)return reject(session.error());row.session.emplace(std::move(*session));
        // Recheck trusted policy after all possible construction callbacks.
        if(auto allowed=authorization_->check(scope);!allowed)return reject(allowed.error());
        auto current=authorization_->session_binding(scope);
        if(!current)return reject(current.error());
        if(*current!=*mapped)return reject(Error::StaleGeneration);
        if(auto valid=validate_connection(row);!valid)return reject(valid.error());
        auto after=authorization_->policy_revision();
        if(!after)return reject(after.error());
        if(*after!=*revision)return reject(Error::StaleGeneration);
        return Handle{i,row.generation};
    }
    return fail(Error::CapacityExceeded);
}
void RtcOwner::detach(Connection& row) noexcept {
    row.closing=true; // Revocation visible before destroying any borrowed object.
    row.session.reset();row.carrier.reset();
}
Status RtcOwner::validate_connection(Connection& row) noexcept {
    if(row.closing||!row.session)return fail(Error::NotReady);
    auto reject=[&](Error error)->Status{
        if(error!=Error::Busy)detach(row);
        return fail(error);
    };
    auto revision=authorization_->policy_revision();
    if(!revision)return reject(revision.error());
    auto now=continuity_.observe(*clock_);
    if(!now)return reject(now.error());
    if(now->proof_generation!=row.continuity_generation)return reject(Error::StaleGeneration);
    if(auto allowed=authorization_->check(row.scope);!allowed)return reject(allowed.error());
    auto mapped=authorization_->session_binding(row.scope);
    if(!mapped)return reject(mapped.error());
    if(*mapped!=row.binding)return reject(Error::StaleGeneration);
    // A mapping callback may revoke permissions or the continuity proof.
    if(auto allowed=authorization_->check(row.scope);!allowed)return reject(allowed.error());
    auto after=continuity_.observe(*clock_);
    if(!after)return reject(after.error());
    if(after->proof_generation!=row.continuity_generation)return reject(Error::StaleGeneration);
    auto current_revision=authorization_->policy_revision();
    if(!current_revision)return reject(current_revision.error());
    if(*current_revision!=*revision)return reject(Error::StaleGeneration);
    return {};
}
Status RtcOwner::pump_session(Handle handle,Tick tick) noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running)return fail(Error::NotReady);
    Enter lock(busy_);
    if(handle.index>=connections_.size())return fail(Error::InvalidArgument);
    auto& row=connections_[handle.index];
    if(!row.occupied||row.generation!=handle.generation)return fail(Error::StaleGeneration);
    if(auto valid=validate_connection(row);!valid)return valid;
    return row.session->pump(tick);
}
RtcOwner::SessionLease::~SessionLease(){
    if(owner_){
        if(std::this_thread::get_id()!=owner_->thread_||!owner_->busy_)std::abort();
        owner_->busy_=false;
    }
}
Result<RtcOwner::SessionLease> RtcOwner::borrow_session(Handle handle) noexcept {
    if(auto s=own();!s)return fail(s.error());
    if(state_!=State::Running)return fail(Error::NotReady);
    if(handle.index>=connections_.size())return fail(Error::InvalidArgument);
    auto& row=connections_[handle.index];
    if(!row.occupied||row.generation!=handle.generation)return fail(Error::StaleGeneration);
    busy_=true;
    if(auto valid=validate_connection(row);!valid){busy_=false;return fail(valid.error());}
    return SessionLease(*this,*row.session);
}
Result<bool> RtcOwner::session_ready(Handle handle) noexcept {
    if(auto s=own();!s)return fail(s.error());
    Enter lock(busy_);
    if(handle.index>=connections_.size())return fail(Error::InvalidArgument);
    auto& row=connections_[handle.index];
    if(!row.occupied||row.generation!=handle.generation)return fail(Error::StaleGeneration);
    if(row.closing||!row.session)return fail(Error::NotReady);
    if(auto valid=validate_connection(row);!valid)return fail(valid.error());
    return row.session->ready();
}
Result<RtcOwner::Diagnostics> RtcOwner::diagnostics() noexcept {
    if(auto s=own();!s)return fail(s.error());
    Enter lock(busy_);Diagnostics result;
    for(const auto& row:pending_)result.pending+=row.occupied;
    for(const auto& row:connections_){
        result.claims+=row.occupied;result.closing_claims+=row.occupied&&row.closing;
        result.sessions+=row.session.has_value();result.session_bytes+=row.allocator.total();
    }
    result.host_bytes=allocator_.total();
    if(native_){auto allocated=native_->allocated();if(!allocated)return fail(allocated.error());
        if(*allocated){auto count=native_->counts();if(!count)return fail(count.error());result.native_live=count->live;}}
    return result;
}
Status RtcOwner::retire(Handle handle) noexcept {
    if(auto s=own();!s)return s;Enter lock(busy_);
    if(handle.index>=connections_.size())return fail(Error::InvalidArgument);
    auto& row=connections_[handle.index];
    if(!row.occupied||row.generation!=handle.generation)return fail(Error::StaleGeneration);
    detach(row);return {}; // logical close; pump owns asynchronous retirement.
}
Status RtcOwner::cancel(pairing::Token token) noexcept {
    if(auto s=own();!s)return s;Enter lock(busy_);
    for(auto& row:pending_)if(row.occupied&&row.token==token){row.closing=true;return {};}
    return fail(Error::StaleGeneration);
}
bool RtcOwner::empty() const noexcept {
    for(const auto& row:pending_)if(row.occupied)return false;
    for(const auto& row:connections_)if(row.occupied)return false;
    return true;
}
Status RtcOwner::pump() noexcept {
    if(auto s=own();!s)return s;
    if(state_!=State::Running&&state_!=State::Stopping)return fail(Error::NotReady);
    Enter lock(busy_);
    if(native_){auto allocated=native_->allocated();if(!allocated)return fail(allocated.error());
        if(*allocated){auto polled=native_->poll(8);if(!polled)return fail(polled.error());}}
    const auto sampled=clock_?clock_->sample():Result<ClockSample>(fail(Error::NotReady));
    for(auto& row:pending_)if(row.occupied&&(!continuity_.ready()||
            (sampled&&sampled->continuous_us>=row.offer.expires_us)))row.closing=true;
    // Fixed total work: eight tracked entries per owner tick, irrespective of
    // peer count. Continue release/cancel even when continuity has been revoked.
    for(std::size_t n=0;n<8;++n){const auto index=cursor_;
        cursor_ = cursor_+1 == pending_.size()+connections_.size() ? 0 : cursor_+1;
        if(index<pending_.size()){
            auto& row=pending_[index];if(!row.occupied)continue;
            if(!row.closing){
                auto allowed=authorization_->check(row.offer.scope);
                if(!allowed&&allowed.error()!=Error::Busy)row.closing=true;
            }
            if(!row.closing)continue;
            auto result=registry_->cancel(row.token);
            if(result||result.error()==Error::StaleGeneration)row={};
            else if(result.error()!=Error::Busy)return result;
        }else{auto& row=connections_[index-pending_.size()];if(!row.occupied)continue;
            if(!row.closing){(void)validate_connection(row);}
            if(!row.closing)continue;
            auto result=registry_->release(row.claim);
            if(result){row.occupied=false;row.closing=false;row.claim={};}
            else if(result.error()!=Error::Busy)return result;
        }
    }
    return state_==State::Stopping&&!empty()?Status(fail(Error::Busy)):Status{};
}
Status RtcOwner::begin_stop() noexcept {
    if(auto s=own();!s)return s;Enter lock(busy_);
    if(state_==State::Stopped)return {};
    state_=State::Stopping;
    for(auto& row:pending_)if(row.occupied)row.closing=true;
    for(auto& row:connections_)if(row.occupied)detach(row);
    return {};
}
Status RtcOwner::stop_step(std::chrono::steady_clock::time_point deadline) noexcept {
    if(auto s=own();!s)return s;
    if(state_==State::Stopped)return {};
    if(state_!=State::Stopping)return fail(Error::NotReady);
    // pump has its own reentry guard. Do not nest it beneath this step's guard.
    auto pumped=pump();if(!pumped)return pumped;
    Enter lock(busy_);
    if(!empty())return fail(Error::Busy);
    // Empty registry first: its destructor can otherwise call native close.
    registry_.reset();
    if(native_){auto shut=native_->shutdown(deadline);if(!shut)return shut;native_.reset();}
    auto stopped=crypto_.stop();if(!stopped)return stopped;
    clock_.reset();state_=State::Stopped;return {};
}
}
