#include "superpos/world.hpp"
#include <cstring>
#include <utility>
#include <atomic>

namespace superpos {
namespace {
Result<std::uint64_t> new_world_instance() noexcept {
    static std::atomic<std::uint64_t> next{1}; auto current=next.load(std::memory_order_relaxed);
    for(;;){if(current==UINT64_MAX)return fail(Error::CounterExhausted);if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current;}
}
struct MutationScope { bool& busy; explicit MutationScope(bool& value) noexcept:busy(value){busy=true;} ~MutationScope(){busy=false;} };
bool storage_overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
}

World::World(World&& other) noexcept { *this=std::move(other); }
World& World::operator=(World&& other) noexcept {
    if(this!=&other){
        config_=other.config_;owner_=other.owner_;
        slots_=std::exchange(other.slots_,{});state_=std::exchange(other.state_,{});schemas_=std::exchange(other.schemas_,{});
        free_head_=std::exchange(other.free_head_,UINT32_MAX);live_=std::exchange(other.live_,0);publication_revision_=std::exchange(other.publication_revision_,0);
        instance_=std::exchange(other.instance_,0);mutating_=false;
    }return *this;
}
Status World::available() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!instance_||slots_.empty()||schemas_.empty()||state_.empty())return fail(Error::NotReady);return {};
}
bool World::mutable_overlap(std::span<const std::byte> input) const noexcept {
    return storage_overlap(input,std::as_bytes(std::span(this,1)))||storage_overlap(input,std::as_bytes(slots_))||storage_overlap(input,state_);
}
Result<Epoch> World::authority_epoch() const noexcept {if(auto ready=available();!ready)return fail(ready.error());if(mutating_)return fail(Error::Busy);return config_.authority_epoch;}
Result<std::size_t> World::live_count() const noexcept {if(auto ready=available();!ready)return fail(ready.error());if(mutating_)return fail(Error::Busy);return live_;}
Result<WorldIdentity> World::identity() const noexcept {if(auto ready=available();!ready)return fail(ready.error());return WorldIdentity{instance_,config_};}
Result<bool> World::storage_overlaps(std::span<const std::byte> input) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());if(mutable_overlap(input)||storage_overlap(input,std::as_bytes(schemas_)))return true;
    for(const auto& schema:schemas_)if(storage_overlap(input,std::as_bytes(schema.fields()))||storage_overlap(input,std::as_bytes(schema.rpcs())))return true;return false;
}
Status World::barrier(CanonicalAuthorityBarrier* gate) noexcept {
    if(!gate)return {};const auto captured=WorldIdentity{instance_,config_};auto authorized=gate->authorize(captured);if(!authorized)return authorized;
    if(captured!=WorldIdentity{instance_,config_})return fail(Error::StaleGeneration);return {};
}

Result<World> World::create(WorldConfig config, std::span<WorldSlot> slots,
    std::span<std::byte> arena, std::span<const Schema> schemas) noexcept {
    if (!config.authority_epoch || !config.state_stride || config.state_stride > Schema::maximum_state_bytes ||
        slots.empty() || slots.size() > UINT32_MAX || schemas.empty() || schemas.size() > UINT32_MAX)
        return fail(Error::InvalidArgument);
    if (slots.size() > arena.size() / config.state_stride) return fail(Error::CapacityExceeded);
    const auto slot_bytes=std::as_bytes(slots),schema_bytes=std::as_bytes(schemas);
    if(storage_overlap(slot_bytes,arena) || storage_overlap(slot_bytes,schema_bytes) || storage_overlap(arena,schema_bytes))return fail(Error::InvalidArgument);
    for (std::size_t i = 0; i < schemas.size(); ++i) {
        if (!schemas[i].id() || schemas[i].state_bytes() > config.state_stride) return fail(Error::IncompatibleSchema);
        for (std::size_t j = 0; j < i; ++j) if (schemas[i].id() == schemas[j].id()) return fail(Error::IncompatibleSchema);
        const auto fields=std::as_bytes(schemas[i].fields()),rpcs=std::as_bytes(schemas[i].rpcs());
        if(storage_overlap(fields,arena) || storage_overlap(fields,slot_bytes) || storage_overlap(rpcs,arena) || storage_overlap(rpcs,slot_bytes))return fail(Error::InvalidArgument);
    }
    auto instance=new_world_instance();if(!instance)return fail(instance.error());
    // Do not touch caller storage until every precondition has passed.
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i] = WorldSlot{};
        slots[i].next_free = i + 1 < slots.size() ? static_cast<std::uint32_t>(i + 1) : UINT32_MAX;
    }
    World world;
    world.config_ = config; world.slots_ = slots; world.state_ = arena;
    world.schemas_ = schemas; world.free_head_ = 0;world.instance_=*instance;
    return world;
}
Result<std::size_t> World::index(ObjectHandle handle) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if (!handle || !handle.slot() || handle.slot() > slots_.size()) return fail(Error::StaleGeneration);
    auto i = static_cast<std::size_t>(handle.slot() - 1);
    const auto& slot = slots_[i];
    if (!slot.live || slot.generation != handle.generation()) return fail(Error::StaleGeneration);
    return i;
}
std::span<std::byte> World::mutable_state(std::size_t i) noexcept {
    return state_.subspan(i * config_.state_stride, schemas_[slots_[i].schema_index].state_bytes());
}
Result<ObjectHandle> World::spawn(SchemaId schema_id, PeerId owner,
    std::span<const std::byte> canonical, Tick tick) noexcept {
    return spawn_impl(schema_id,owner,canonical,tick,nullptr);
}
Result<ObjectHandle> World::spawn(SchemaId id,PeerId owner,std::span<const std::byte> canonical,Tick tick,CanonicalAuthorityBarrier& gate) noexcept {return spawn_impl(id,owner,canonical,tick,&gate);}
Result<ObjectHandle> World::spawn_impl(SchemaId schema_id,PeerId owner,std::span<const std::byte> canonical,Tick tick,CanonicalAuthorityBarrier* gate) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(mutating_)return fail(Error::Busy);MutationScope call(mutating_);
    // Copying an existing canonical state is allowed, but metadata/manager bytes
    // must never become a source changed by the spawn's own free-list mutation.
    if(storage_overlap(canonical,std::as_bytes(slots_))||storage_overlap(canonical,std::as_bytes(std::span(this,1))))return fail(Error::InvalidArgument);
    std::size_t schema_index{};
    while (schema_index < schemas_.size() && schemas_[schema_index].id() != schema_id) ++schema_index;
    if (schema_index == schemas_.size()) return fail(Error::IncompatibleSchema);
    if (auto s = schemas_[schema_index].validate(canonical); !s) return fail(s.error());
    if (free_head_ == UINT32_MAX) return fail(Error::CapacityExceeded);
    if(auto authorized=barrier(gate);!authorized)return fail(authorized.error());
    auto i = free_head_; auto& slot = slots_[i];
    free_head_ = slot.next_free;
    slot.live = true; slot.next_free = UINT32_MAX; slot.schema_index = static_cast<std::uint32_t>(schema_index);
    slot.owner = owner; slot.ownership_revision = 1; slot.revision = 1; slot.tick = tick;
    std::memmove(mutable_state(i).data(), canonical.data(), canonical.size());
    ++live_;
    return ObjectHandle::from_parts(i + 1, slot.generation);
}
Status World::destroy(ObjectHandle handle, Epoch epoch) noexcept {
    return destroy_impl(handle,epoch,nullptr);
}
Status World::destroy(ObjectHandle handle,Epoch epoch,CanonicalAuthorityBarrier& gate) noexcept {return destroy_impl(handle,epoch,&gate);}
Status World::destroy_impl(ObjectHandle handle,Epoch epoch,CanonicalAuthorityBarrier* gate) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(mutating_)return fail(Error::Busy);MutationScope call(mutating_);
    if (epoch != config_.authority_epoch) return fail(Error::StaleEpoch);
    auto found = index(handle); if (!found) return fail(found.error());
    auto i = static_cast<std::uint32_t>(*found); auto& slot = slots_[i];
    if(auto authorized=barrier(gate);!authorized)return authorized;
    auto bytes = mutable_state(i); std::memset(bytes.data(), 0, bytes.size());
    slot.live = false; --live_;
    if (slot.generation == UINT32_MAX) { slot.retired = true; return {}; }
    ++slot.generation; slot.next_free = free_head_; free_head_ = i;
    return {};
}
Result<EntityView> World::view(ObjectHandle handle) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());if(mutating_)return fail(Error::Busy);
    auto found = index(handle); if (!found) return fail(found.error());
    const auto& slot = slots_[*found]; const auto& schema = schemas_[slot.schema_index];
    return EntityView{handle, &schema, slot.owner, slot.ownership_revision, slot.revision,
        slot.tick, state_.subspan(*found * config_.state_stride, schema.state_bytes())};
}
Result<std::uint64_t> World::publish(std::span<const Publication> publications, Tick tick, Epoch epoch) noexcept {
    return publish_impl(publications,tick,epoch,nullptr);
}
Result<std::uint64_t> World::publish(std::span<const Publication> p,Tick tick,Epoch epoch,CanonicalAuthorityBarrier& gate) noexcept {return publish_impl(p,tick,epoch,&gate);}
Result<std::uint64_t> World::publish_impl(std::span<const Publication> publications,Tick tick,Epoch epoch,CanonicalAuthorityBarrier* gate) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(mutating_)return fail(Error::Busy);MutationScope call(mutating_);
    if (epoch != config_.authority_epoch) return fail(Error::StaleEpoch);
    if (publications.empty() || publications.size() > maximum_group_objects) return fail(Error::InvalidArgument);
    // Complete input preflight precedes every write: a descriptor or any later
    // canonical source must not be changed by applying an earlier group member.
    if(mutable_overlap(std::as_bytes(publications)))return fail(Error::InvalidArgument);
    for(const auto& publication:publications)if(mutable_overlap(publication.canonical))return fail(Error::InvalidArgument);
    if (publication_revision_ == UINT64_MAX) return fail(Error::CounterExhausted);
    std::size_t bytes{};
    for (std::size_t n = 0; n < publications.size(); ++n) {
        const auto& p = publications[n];
        auto i = index(p.handle); if (!i) return fail(i.error());
        const auto& slot = slots_[*i];
        if (p.expected_revision != slot.revision) return fail(Error::Busy);
        if (slot.revision == UINT64_MAX) return fail(Error::CounterExhausted);
        if (tick < slot.tick) return fail(Error::InvalidArgument);
        if (auto s = schemas_[slot.schema_index].validate(p.canonical); !s) return fail(s.error());
        if (p.canonical.size() > maximum_group_bytes - bytes) return fail(Error::CapacityExceeded);
        bytes += p.canonical.size();
        for (std::size_t j = 0; j < n; ++j) if (publications[j].handle == p.handle) return fail(Error::InvalidArgument);
    }
    if(auto authorized=barrier(gate);!authorized)return fail(authorized.error());
    for (const auto& p : publications) {
        auto i = static_cast<std::size_t>(p.handle.slot() - 1); auto& slot = slots_[i];
        std::memcpy(mutable_state(i).data(), p.canonical.data(), p.canonical.size());
        ++slot.revision; slot.tick = tick;
    }
    return ++publication_revision_;
}
Result<std::uint64_t> World::transfer_ownership(ObjectHandle handle, PeerId new_owner,
    std::uint64_t expected, Tick tick, Epoch epoch) noexcept {
    return transfer_impl(handle,new_owner,expected,tick,epoch,nullptr);
}
Result<std::uint64_t> World::transfer_ownership(ObjectHandle handle,PeerId owner,std::uint64_t expected,Tick tick,Epoch epoch,CanonicalAuthorityBarrier& gate) noexcept {return transfer_impl(handle,owner,expected,tick,epoch,&gate);}
Result<std::uint64_t> World::transfer_impl(ObjectHandle handle,PeerId new_owner,std::uint64_t expected,Tick tick,Epoch epoch,CanonicalAuthorityBarrier* gate) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(mutating_)return fail(Error::Busy);MutationScope call(mutating_);
    if (epoch != config_.authority_epoch) return fail(Error::StaleEpoch);
    auto i = index(handle); if (!i) return fail(i.error());
    auto& slot = slots_[*i];
    if (slot.ownership_revision != expected) return fail(Error::PermissionDenied);
    if (tick < slot.tick) return fail(Error::InvalidArgument);
    if (slot.ownership_revision == UINT64_MAX || slot.revision == UINT64_MAX) return fail(Error::CounterExhausted);
    if(auto authorized=barrier(gate);!authorized)return fail(authorized.error());
    slot.owner = new_owner; ++slot.ownership_revision; ++slot.revision; slot.tick = tick;
    return slot.ownership_revision;
}
Status World::authorize_rpc(ObjectHandle handle, RpcId rpc_id, const RpcContext& context,
    std::size_t payload_bytes) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(mutating_)return fail(Error::Busy);
    if (context.authority_epoch != config_.authority_epoch) return fail(Error::StaleEpoch);
    auto i = index(handle); if (!i) return fail(i.error());
    const auto& slot = slots_[*i]; auto rpc = schemas_[slot.schema_index].rpc(rpc_id);
    if (!rpc) return fail(Error::Unsupported);
    if (!context.admitted || payload_bytes > rpc->maximum_payload) return fail(Error::PermissionDenied);
    switch (rpc->permission) {
    case RpcPermission::Authority:
        if (context.sender != config_.authority_peer) return fail(Error::PermissionDenied);
        break;
    case RpcPermission::Owner:
        if (context.sender != slot.owner || context.ownership_revision != slot.ownership_revision) return fail(Error::PermissionDenied);
        break;
    case RpcPermission::AdmittedPeer: break;
    }
    return {};
}
}
