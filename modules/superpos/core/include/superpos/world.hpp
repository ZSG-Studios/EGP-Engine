#pragma once

#include "schema.hpp"
#include <limits>
#include <thread>

namespace superpos {

struct WorldSlot {
    std::uint32_t generation{1};
    std::uint32_t next_free{UINT32_MAX};
    std::uint32_t schema_index{};
    bool live{};
    bool retired{};
    PeerId owner{};
    std::uint64_t ownership_revision{};
    std::uint64_t revision{};
    Tick tick{};
};
struct WorldConfig { Epoch authority_epoch{1}; PeerId authority_peer{}; std::size_t state_stride{64}; bool operator==(const WorldConfig&) const noexcept=default; };
// Local creation identity survives a quiescent move and is never reused in this
// process. It is neither a wire identity nor an admission/recovery credential.
struct WorldIdentity { std::uint64_t instance{}; WorldConfig config{}; bool operator==(const WorldIdentity&) const noexcept=default; };
// Called after complete mutation preflight at the no-callback canonical write
// barrier. A trusted implementation may fail; it must not mutate caller inputs,
// move/replace the World or invoke direct APIs on borrowed components.
class CanonicalAuthorityBarrier {
public:
    virtual ~CanonicalAuthorityBarrier()=default;
    virtual Status authorize(const WorldIdentity&) noexcept=0;
};
struct EntityView {
    ObjectHandle handle{};
    const Schema* schema{};
    PeerId owner{};
    std::uint64_t ownership_revision{};
    std::uint64_t revision{};
    Tick tick{};
    std::span<const std::byte> canonical;
};
struct Publication {
    ObjectHandle handle{};
    std::uint64_t expected_revision{};
    std::span<const std::byte> canonical;
};
// Construct this context from authenticated admission/session state, never from
// untrusted RPC payload flags. Ownership is rechecked at execution time.
struct RpcContext {
    PeerId sender{};
    Epoch authority_epoch{};
    std::uint64_t ownership_revision{};
    bool admitted{};
};

// Owner-thread registry backed entirely by caller-owned bounded storage. Views
// remain valid until the next mutation; schemas/storage outlive the world.
// Moves preserve the creating thread and invalidate the source. Move/replacement
// and borrowed storage mutation require exclusive ownership, with no live call.
class World {
    WorldConfig config_;
    std::span<WorldSlot> slots_;
    std::span<std::byte> state_;
    std::span<const Schema> schemas_;
    std::uint32_t free_head_{UINT32_MAX};
    std::size_t live_{};
    std::uint64_t publication_revision_{};
    std::uint64_t instance_{};
    bool mutating_{};
    std::thread::id owner_{std::this_thread::get_id()};
    Status available() const noexcept;
    bool mutable_overlap(std::span<const std::byte>) const noexcept;
    Result<std::size_t> index(ObjectHandle) const noexcept;
    std::span<std::byte> mutable_state(std::size_t) noexcept;
    Status barrier(CanonicalAuthorityBarrier*) noexcept;
    Result<ObjectHandle> spawn_impl(SchemaId,PeerId,std::span<const std::byte>,Tick,CanonicalAuthorityBarrier*) noexcept;
    Status destroy_impl(ObjectHandle,Epoch,CanonicalAuthorityBarrier*) noexcept;
    Result<std::uint64_t> publish_impl(std::span<const Publication>,Tick,Epoch,CanonicalAuthorityBarrier*) noexcept;
    Result<std::uint64_t> transfer_impl(ObjectHandle,PeerId,std::uint64_t,Tick,Epoch,CanonicalAuthorityBarrier*) noexcept;
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    static constexpr std::size_t maximum_group_objects = 16;
    static constexpr std::size_t maximum_group_bytes = 64 * 1024;
    static Result<World> create(WorldConfig, std::span<WorldSlot>,
        std::span<std::byte> state_arena, std::span<const Schema>) noexcept;
    Result<ObjectHandle> spawn(SchemaId, PeerId owner, std::span<const std::byte>, Tick) noexcept;
    Result<ObjectHandle> spawn(SchemaId,PeerId,std::span<const std::byte>,Tick,CanonicalAuthorityBarrier&) noexcept;
    Status destroy(ObjectHandle, Epoch) noexcept;
    Status destroy(ObjectHandle,Epoch,CanonicalAuthorityBarrier&) noexcept;
    Result<EntityView> view(ObjectHandle) const noexcept;
    Result<std::uint64_t> publish(std::span<const Publication>, Tick, Epoch) noexcept;
    Result<std::uint64_t> publish(std::span<const Publication>,Tick,Epoch,CanonicalAuthorityBarrier&) noexcept;
    Result<std::uint64_t> transfer_ownership(ObjectHandle, PeerId new_owner,
        std::uint64_t expected_ownership_revision, Tick, Epoch) noexcept;
    Result<std::uint64_t> transfer_ownership(ObjectHandle,PeerId,std::uint64_t,Tick,Epoch,CanonicalAuthorityBarrier&) noexcept;
    Status authorize_rpc(ObjectHandle, RpcId, const RpcContext&, std::size_t payload_bytes) const noexcept;
    Result<Epoch> authority_epoch() const noexcept;
    Result<std::size_t> live_count() const noexcept;
    // identity remains available during a barrier for its exact binding check;
    // ordinary views and other APIs reject reentry while a mutation is active.
    Result<WorldIdentity> identity() const noexcept;
    Result<bool> storage_overlaps(std::span<const std::byte>) const noexcept;
};

}
