#include "superpos/world_authority.hpp"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <new>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty()||b.empty())return false;const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());return x<=y?y-x<a.size():x-y<b.size();
}
template<class T>struct Restore { T& value;T old;Restore(T& target,T replacement) noexcept:value(target),old(target){target=replacement;}~Restore(){value=old;} };
Status valid_config(AuthorityBindingConfig config) noexcept {if(!config.match||static_cast<unsigned>(config.kind)>1)return fail(Error::InvalidArgument);return {};}
Result<std::uint64_t> new_binding_instance() noexcept {static std::atomic<std::uint64_t> next{1};auto current=next.load(std::memory_order_relaxed);for(;;){if(current==UINT64_MAX)return fail(Error::CounterExhausted);if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current;}}
}
WorldAuthorityBinding::WorldAuthorityBinding(RegisteredWorld& world,Session& session,AuthorityLease& lease,AuthorityBindingConfig config,
    std::span<AuthorityPublicationCapture> captures,std::span<std::byte> arena,Validated checked) noexcept:
    registered_(&world),world_(&world.world_),session_(&session),lease_(&lease),config_(config),scope_(checked.scope),lease_scope_(checked.lease),captures_(captures),arena_(arena),owner_(std::this_thread::get_id()) {}
Result<WorldAuthorityBinding> WorldAuthorityBinding::create(RegisteredWorld& registered,Session& session,AuthorityLease& lease,AuthorityBindingConfig config,
    std::span<AuthorityPublicationCapture> captures,std::span<std::byte> arena) noexcept {
    if(auto valid=valid_config(config);!valid)return fail(valid.error());if(captures.empty()||captures.size()>maximum_captures||arena.size()<captures.size()*capture_bytes)return fail(Error::CapacityExceeded);
    auto world=registered.world_.identity();if(!world)return fail(world.error());auto identity=session.identity();if(!identity)return fail(identity.error());auto lease_scope=lease.scope_identity();if(!lease_scope)return fail(lease_scope.error());
    if(!registered.frozen_.size()||registered.fingerprint()!=identity->schemas)return fail(Error::IncompatibleSchema);
    if(world->config.authority_peer!=identity->local_peer)return fail(Error::PermissionDenied);
    const auto records=std::as_bytes(registered.frozen_.records_),state=std::as_bytes(std::span(registered.frozen_.state_,1));
    const std::array pools{std::as_bytes(captures),std::span<const std::byte>(arena)};
    if(overlap(pools[0],pools[1]))return fail(Error::InvalidArgument);
    for(auto pool:pools){auto a=registered.world_.storage_overlaps(pool),b=session.storage_overlaps(pool);if(!a)return fail(a.error());if(!b)return fail(b.error());
        if(*a||*b||overlap(pool,std::as_bytes(std::span(&registered,1)))||overlap(pool,std::as_bytes(std::span(&lease,1)))||overlap(pool,records)||overlap(pool,state))return fail(Error::InvalidArgument);}
    auto authorized=lease.authorize(world->config.authority_peer,config.kind,world->config.authority_epoch,config.membership);if(!authorized)return fail(authorized.error());
    // No readiness callback follows the final time observation.
    auto final_world=registered.world_.identity();auto final_session=session.instance_identity();auto final_lease=lease.scope_identity();if(!final_world)return fail(final_world.error());if(!final_session)return fail(final_session.error());if(!final_lease)return fail(final_lease.error());
    if(*final_world!=*world||*final_session!=identity->instance||*final_lease!=*lease_scope)return fail(Error::StaleGeneration);
    auto instance=new_binding_instance();if(!instance)return fail(instance.error());for(auto& capture:captures)capture={};
    return Result<WorldAuthorityBinding>(std::in_place,registered,session,lease,config,captures,arena,Validated({config.match,*world,*identity,*instance},*lease_scope));
}
Status WorldAuthorityBinding::validate() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(invalidated_)return fail(Error::StaleGeneration);
    lease_=std::launder(lease_);auto lease_scope=lease_->scope_identity();if(!lease_scope)return fail(lease_scope.error());if(*lease_scope!=lease_scope_){invalidated_=true;return fail(Error::StaleGeneration);}
    auto world=world_->identity();auto identity=session_->identity();
    if(!world||!identity){invalidated_=true;return fail(!world?world.error():identity.error());}
    if(*world!=scope_.world||*identity!=scope_.session||!registered_->frozen_.size()||registered_->fingerprint()!=scope_.session.schemas){invalidated_=true;return fail(Error::StaleGeneration);}return {};
}
Status WorldAuthorityBinding::entry(const AuthorityWorkScope& scope) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(operation_!=Operation::Idle)return fail(Error::Busy);
    if(scope!=scope_)return fail(Error::StaleGeneration);Restore call(operation_,Operation::Queue);return validate();
}
Status WorldAuthorityBinding::fresh_authority() noexcept {
    if(auto current=validate();!current)return current;
    auto authorized=lease_->authorize(scope_.world.config.authority_peer,config_.kind,scope_.world.config.authority_epoch,config_.membership);if(!authorized)return fail(authorized.error());
    auto world=world_->identity();auto session=session_->instance_identity();auto lease_scope=lease_->scope_identity();if(!world||!session||!lease_scope){invalidated_=true;return fail(!world?world.error():!session?session.error():lease_scope.error());}
    if(*world!=scope_.world||*session!=scope_.session.instance||*lease_scope!=lease_scope_){invalidated_=true;return fail(Error::StaleGeneration);}return {};
}
Status WorldAuthorityBinding::authorize(const WorldIdentity& world) noexcept {
    if(operation_!=Operation::Canonical)return fail(Error::Busy);if(world!=scope_.world)return fail(Error::StaleGeneration);return fresh_authority();
}
Result<AuthorityWorkScope> WorldAuthorityBinding::context() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(operation_!=Operation::Idle)return fail(Error::Busy);
    Restore call(operation_,Operation::Queue);if(auto valid=validate();!valid)return fail(valid.error());return scope_;
}
Result<bool> WorldAuthorityBinding::overlaps(std::span<const std::byte> input) const noexcept {
    if(overlap(input,std::as_bytes(std::span(this,1)))||overlap(input,std::as_bytes(captures_))||overlap(input,arena_)||overlap(input,std::as_bytes(std::span(registered_,1)))||overlap(input,std::as_bytes(std::span(lease_,1))))return true;
    auto a=world_->storage_overlaps(input),b=session_->storage_overlaps(input);if(!a)return fail(a.error());if(!b)return fail(b.error());
    return *a||*b||overlap(input,std::as_bytes(registered_->frozen_.records_))||overlap(input,std::as_bytes(std::span(registered_->frozen_.state_,1)));
}
Status WorldAuthorityBinding::group_contract(std::span<const Publication> updates) const noexcept {
    std::size_t bytes{};for(const auto& update:updates){if(update.canonical.size()>capture_bytes-bytes)return fail(Error::CapacityExceeded);bytes+=update.canonical.size();}
    std::uint64_t group{};bool have_group{};
    for(const auto& update:updates){auto view=world_->view(update.handle);if(!view)return fail(view.error());auto schema=registered_->frozen_.find(view->schema->id());if(!schema)return fail(schema.error());const auto& contract=(*schema)->contract;
        if(updates.size()>contract.maximum_group_objects||bytes>contract.maximum_group_bytes)return fail(Error::CapacityExceeded);
        if(updates.size()>1){if(contract.publication!=PublicationSemantics::ExplicitAtomicGroup)return fail(Error::Unsupported);if(have_group&&group!=contract.group_id)return fail(Error::IncompatibleSchema);group=contract.group_id;have_group=true;}}
    return {};
}
Result<std::uint64_t> WorldAuthorityBinding::queue_publication(const AuthorityWorkScope& scope,std::span<const Publication> group,Tick tick) noexcept {
    if(auto allowed=entry(scope);!allowed)return fail(allowed.error());Restore call(operation_,Operation::Queue);
    if(group.empty()||group.size()>World::maximum_group_objects)return fail(Error::InvalidArgument);
    auto descriptors=overlaps(std::as_bytes(group));if(!descriptors)return fail(descriptors.error());if(*descriptors)return fail(Error::InvalidArgument);
    std::size_t bytes{};
    for(std::size_t n=0;n<group.size();++n){const auto& p=group[n];auto alias=overlaps(p.canonical);if(!alias)return fail(alias.error());if(*alias)return fail(Error::InvalidArgument);
        auto view=world_->view(p.handle);if(!view)return fail(view.error());if(view->revision!=p.expected_revision)return fail(Error::Busy);if(tick<view->tick)return fail(Error::InvalidArgument);
        if(auto valid=view->schema->validate(p.canonical);!valid)return fail(valid.error());if(p.canonical.size()>capture_bytes-bytes)return fail(Error::CapacityExceeded);bytes+=p.canonical.size();
        for(std::size_t prior=0;prior<n;++prior)if(group[prior].handle==p.handle)return fail(Error::InvalidArgument);}
    if(auto contract=group_contract(group);!contract)return fail(contract.error());
    AuthorityPublicationCapture* vacancy=nullptr;for(auto& capture:captures_)if(!capture.occupied){vacancy=&capture;break;}if(!vacancy)return fail(Error::Busy);if(order_==UINT64_MAX)return fail(Error::CounterExhausted);
    if(auto authorized=fresh_authority();!authorized)return fail(authorized.error());
    auto destination=arena_.subspan(static_cast<std::size_t>(vacancy-captures_.data())*capture_bytes,capture_bytes);AuthorityPublicationCapture next;next.scope=scope_;next.tick=tick;next.count=static_cast<std::uint16_t>(group.size());next.bytes=static_cast<std::uint32_t>(bytes);next.order=order_+1;
    std::size_t offset{};for(std::size_t n=0;n<group.size();++n){auto copy=destination.subspan(offset,group[n].canonical.size());std::memcpy(copy.data(),group[n].canonical.data(),copy.size());next.members[n]={group[n].handle,group[n].expected_revision,copy};offset+=copy.size();}
    next.occupied=true;*vacancy=next;return ++order_;
}
Result<AuthorityPublicationApplied> WorldAuthorityBinding::apply_next(const AuthorityWorkScope& scope) noexcept {
    if(auto allowed=entry(scope);!allowed)return fail(allowed.error());Restore call(operation_,Operation::Canonical);
    AuthorityPublicationCapture* next=nullptr;for(auto& capture:captures_)if(capture.occupied&&(!next||capture.order<next->order))next=&capture;if(!next)return fail(Error::NotReady);
    if(next->scope!=scope_)return fail(Error::StaleGeneration);
    if(auto contract=group_contract(std::span(next->members).first(next->count));!contract)return fail(contract.error());
    auto applied=world_->publish(std::span(next->members).first(next->count),next->tick,scope_.world.config.authority_epoch,*this);
    if(!applied)return fail(applied.error());const auto order=next->order;*next={};return AuthorityPublicationApplied{order,*applied};
}
Result<ObjectHandle> WorldAuthorityBinding::spawn(const AuthorityWorkScope& scope,SchemaId schema,PeerId owner,std::span<const std::byte> bytes,Tick tick) noexcept {
    if(auto allowed=entry(scope);!allowed)return fail(allowed.error());Restore call(operation_,Operation::Canonical);auto alias=overlaps(bytes);if(!alias)return fail(alias.error());if(*alias)return fail(Error::InvalidArgument);return world_->spawn(schema,owner,bytes,tick,*this);
}
Status WorldAuthorityBinding::destroy(const AuthorityWorkScope& scope,ObjectHandle handle) noexcept {if(auto allowed=entry(scope);!allowed)return allowed;Restore call(operation_,Operation::Canonical);return world_->destroy(handle,scope_.world.config.authority_epoch,*this);}
Result<std::uint64_t> WorldAuthorityBinding::transfer_ownership(const AuthorityWorkScope& scope,ObjectHandle handle,PeerId peer,std::uint64_t revision,Tick tick) noexcept {if(auto allowed=entry(scope);!allowed)return fail(allowed.error());Restore call(operation_,Operation::Canonical);return world_->transfer_ownership(handle,peer,revision,tick,scope_.world.config.authority_epoch,*this);}
Status WorldAuthorityBinding::authorize_rpc(const AuthorityWorkScope& scope,ObjectHandle handle,RpcId rpc,std::uint64_t revision,std::size_t bytes) noexcept {
    if(auto allowed=entry(scope);!allowed)return allowed;Restore call(operation_,Operation::Rpc);
    if(auto permission=world_->authorize_rpc(handle,rpc,{scope_.session.remote_peer,scope_.world.config.authority_epoch,revision,true},bytes);!permission)return permission;return fresh_authority();
}
Status WorldAuthorityBinding::discard_pending() noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(operation_!=Operation::Idle)return fail(Error::Busy);for(auto& capture:captures_)capture={};return {};}
Result<std::size_t> WorldAuthorityBinding::pending() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(operation_!=Operation::Idle)return fail(Error::Busy);std::size_t count{};for(const auto& capture:captures_)count+=capture.occupied;return count;}
Status WorldAuthorityBinding::authorize_egress(const AuthorityWorkScope& scope) noexcept {if(auto allowed=entry(scope);!allowed)return allowed;Restore call(operation_,Operation::Egress);return fresh_authority();}

WorldAuthorityDatagramIO::WorldAuthorityDatagramIO(DatagramIO& io,const World& world,AuthorityLease& lease,AuthorityBindingConfig config,Validated captured) noexcept:
    io_(&io),world_(&world),lease_(&lease),config_(config),world_identity_(captured.world),lease_scope_(captured.lease),owner_(std::this_thread::get_id()) {}
Result<WorldAuthorityDatagramIO> WorldAuthorityDatagramIO::create(DatagramIO& io,const World& world,AuthorityLease& lease,AuthorityBindingConfig config) noexcept {
    if(auto valid=valid_config(config);!valid)return fail(valid.error());auto identity=world.identity();if(!identity)return fail(identity.error());auto scope=lease.scope_identity();if(!scope)return fail(scope.error());
    auto authorized=lease.authorize(identity->config.authority_peer,config.kind,identity->config.authority_epoch,config.membership);if(!authorized)return fail(authorized.error());
    auto final=world.identity();auto final_scope=lease.scope_identity();if(!final)return fail(final.error());if(!final_scope)return fail(final_scope.error());if(*final!=*identity||*scope!=*final_scope)return fail(Error::StaleGeneration);
    return Result<WorldAuthorityDatagramIO>(std::in_place,io,world,lease,config,Validated(*identity,*scope));
}
Status WorldAuthorityDatagramIO::attach(WorldAuthorityBinding& binding) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);if(binding_)return fail(Error::Busy);Restore call(busy_,true);
    auto context=binding.context();if(!context)return fail(context.error());if(binding.world_!=world_||binding.lease_!=lease_||binding.config_!=config_||context->world!=world_identity_||binding.lease_scope_!=lease_scope_)return fail(Error::StaleGeneration);
    if(auto live=binding.authorize_egress(*context);!live)return live;binding_=&binding;scope_=*context;return {};
}
Status WorldAuthorityDatagramIO::check() noexcept {
    if(invalidated_)return fail(Error::StaleGeneration);lease_=std::launder(lease_);auto scope=lease_->scope_identity();if(!scope)return fail(scope.error());if(*scope!=lease_scope_){invalidated_=true;return fail(Error::StaleGeneration);}if(binding_)return binding_->authorize_egress(scope_);
    auto identity=world_->identity();if(!identity)return fail(identity.error());if(*identity!=world_identity_){invalidated_=true;return fail(Error::StaleGeneration);}
    auto authorized=lease_->authorize(identity->config.authority_peer,config_.kind,identity->config.authority_epoch,config_.membership);if(!authorized)return fail(authorized.error());
    auto final=world_->identity();auto final_scope=lease_->scope_identity();if(!final)return fail(final.error());if(!final_scope)return fail(final_scope.error());if(*final!=world_identity_||*final_scope!=lease_scope_){invalidated_=true;return fail(Error::StaleGeneration);}return {};
}
Result<std::size_t> WorldAuthorityDatagramIO::send(std::span<const std::byte> bytes) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);
    // Manager object representations are valid const byte views, but changing
    // busy/authorization state could mutate that exact input before raw IO.
    // Session-owned encoded buffers remain legitimate input: reject managers,
    // rather than all backing pools as the receive-output guard must do.
    if(overlap(bytes,std::as_bytes(std::span(this,1)))||overlap(bytes,std::as_bytes(std::span(lease_,1)))||overlap(bytes,std::as_bytes(std::span(world_,1)))||
        (binding_&&(overlap(bytes,std::as_bytes(std::span(binding_,1)))||overlap(bytes,std::as_bytes(std::span(binding_->session_,1))))))return fail(Error::InvalidArgument);
    Restore call(busy_,true);if(auto live=check();!live)return fail(live.error());
    if(binding_){Restore egress(binding_->operation_,WorldAuthorityBinding::Operation::Egress);return io_->send(bytes);}return io_->send(bytes);
}
Result<std::size_t> WorldAuthorityDatagramIO::receive(std::span<std::byte> bytes) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);Restore call(busy_,true);
    if(overlap(bytes,std::as_bytes(std::span(this,1)))||overlap(bytes,std::as_bytes(std::span(lease_,1))))return fail(Error::InvalidArgument);
    auto alias=binding_?binding_->overlaps(bytes):world_->storage_overlaps(bytes);if(!alias)return fail(alias.error());if(*alias)return fail(Error::InvalidArgument);
    if(auto live=check();!live)return fail(live.error());
    if(binding_){Restore egress(binding_->operation_,WorldAuthorityBinding::Operation::Egress);return io_->receive(bytes);}return io_->receive(bytes);
}
}
