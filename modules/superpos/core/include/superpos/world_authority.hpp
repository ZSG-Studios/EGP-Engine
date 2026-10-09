#pragma once
#include "registry.hpp"
#include "session.hpp"
#include "lease.hpp"

namespace superpos {
struct AuthorityBindingConfig {
    // Trusted runtime match namespace: derive/validate it from authenticated
    // match routing for this actual World/Session/lease. V1 grant and HELLO do
    // not authenticate a match field; this value is no cross-match auth proof.
    std::uint64_t match{},membership{}; AuthorityKind kind{};
    bool operator==(const AuthorityBindingConfig&) const noexcept=default;
};
// Local immutable work stamp. Matching it establishes scope, never current
// permission. Every barrier reauthorizes the qualified live lease/continuity.
struct AuthorityWorkScope {
    std::uint64_t match{}; WorldIdentity world{}; SessionIdentity session{};std::uint64_t binding_instance{};
    bool operator==(const AuthorityWorkScope&) const noexcept=default;
};
struct AuthorityPublicationCapture {
    bool occupied{}; std::uint64_t order{}; Tick tick{}; std::uint16_t count{};
    std::uint32_t bytes{}; AuthorityWorkScope scope{};
    std::array<Publication,World::maximum_group_objects> members{};
};
struct AuthorityPublicationApplied { std::uint64_t order{},publication{}; };
class WorldAuthorityDatagramIO;
// An immovable owner-thread binding to an ACTUAL admitted Session and World.
// World, Session, lease and exclusive caller pools outlive it. Qualified trusted
// callbacks cannot directly mutate/move any borrowed component or input buffer.
// Only this binding may mutate its World after attachment; a higher runtime must
// not expose direct World APIs. There is no durable commit/effect/physics claim.
// The runtime validates config.match at its authenticated admission boundary.
class WorldAuthorityBinding final:private CanonicalAuthorityBarrier {
    friend class WorldAuthorityDatagramIO;
    struct Validated { AuthorityWorkScope scope;LeaseScope lease;explicit Validated(AuthorityWorkScope v,LeaseScope l) noexcept:scope(v),lease(l){} };
public:
    static constexpr std::size_t maximum_captures=4,capture_bytes=World::maximum_group_bytes;
    static Result<WorldAuthorityBinding> create(RegisteredWorld&,Session&,AuthorityLease&,AuthorityBindingConfig,
        std::span<AuthorityPublicationCapture>,std::span<std::byte> immutable_arena) noexcept;
    WorldAuthorityBinding(RegisteredWorld&,Session&,AuthorityLease&,AuthorityBindingConfig,std::span<AuthorityPublicationCapture>,std::span<std::byte>,Validated) noexcept;
    WorldAuthorityBinding(const WorldAuthorityBinding&)=delete;
    WorldAuthorityBinding& operator=(const WorldAuthorityBinding&)=delete;
    WorldAuthorityBinding(WorldAuthorityBinding&&)=delete;
    WorldAuthorityBinding& operator=(WorldAuthorityBinding&&)=delete;
    Result<AuthorityWorkScope> context() noexcept;
    Result<std::uint64_t> queue_publication(const AuthorityWorkScope&,std::span<const Publication>,Tick) noexcept;
    Result<AuthorityPublicationApplied> apply_next(const AuthorityWorkScope&) noexcept;
    Result<ObjectHandle> spawn(const AuthorityWorkScope&,SchemaId,PeerId,std::span<const std::byte>,Tick) noexcept;
    Status destroy(const AuthorityWorkScope&,ObjectHandle) noexcept;
    Result<std::uint64_t> transfer_ownership(const AuthorityWorkScope&,ObjectHandle,PeerId,std::uint64_t,Tick) noexcept;
    // Execution-time ownership/admission check; do not cache this as permission
    // for an asynchronous RPC or perform irreversible effects from its result.
    Status authorize_rpc(const AuthorityWorkScope&,ObjectHandle,RpcId,std::uint64_t ownership_revision,std::size_t bytes) noexcept;
    // Owner-only cancellation remains available after lost route/lease. No
    // queued work is transferred to a recreated binding or new match/epoch.
    Status discard_pending() noexcept;
    Result<std::size_t> pending() const noexcept;
private:
    enum class Operation { Idle,Canonical,Queue,Egress,Rpc };
    Status entry(const AuthorityWorkScope&) noexcept;
    Status validate() noexcept;
    Status fresh_authority() noexcept;
    Status group_contract(std::span<const Publication>) const noexcept;
    Status authorize(const WorldIdentity&) noexcept override;
    Status authorize_egress(const AuthorityWorkScope&) noexcept;
    Result<bool> overlaps(std::span<const std::byte>) const noexcept;
    RegisteredWorld* registered_{}; World* world_{}; Session* session_{}; AuthorityLease* lease_{}; AuthorityBindingConfig config_{};
    AuthorityWorkScope scope_{};LeaseScope lease_scope_{};std::span<AuthorityPublicationCapture> captures_{}; std::span<std::byte> arena_{};
    std::uint64_t order_{}; std::thread::id owner_{}; Operation operation_{}; bool invalidated_{};
};

// Place below DTLS/PacketTransport at the actual native DatagramIO boundary, so
// advance()/pending-WRITE retries cannot bypass a live authorization check.
// Bootstrap allows leased HANDSHAKE/ADMISSION ONLY under the trusted runtime's
// exclusive ownership. Do not expose a ready Session/gameplay before attach().
// Attachment is one-way to an already admitted WorldAuthorityBinding. All later
// sends and receives check its exact session/match/World instance. Received
// bytes are only transport input; canonical application has a separate barrier.
class WorldAuthorityDatagramIO final:public DatagramIO {
    struct Validated { WorldIdentity world;LeaseScope lease;explicit Validated(WorldIdentity v,LeaseScope l) noexcept:world(v),lease(l){} };
public:
    static Result<WorldAuthorityDatagramIO> create(DatagramIO&,const World&,AuthorityLease&,AuthorityBindingConfig) noexcept;
    WorldAuthorityDatagramIO(DatagramIO&,const World&,AuthorityLease&,AuthorityBindingConfig,Validated) noexcept;
    WorldAuthorityDatagramIO(const WorldAuthorityDatagramIO&)=delete;
    WorldAuthorityDatagramIO& operator=(const WorldAuthorityDatagramIO&)=delete;
    WorldAuthorityDatagramIO(WorldAuthorityDatagramIO&&)=delete;
    WorldAuthorityDatagramIO& operator=(WorldAuthorityDatagramIO&&)=delete;
    Status attach(WorldAuthorityBinding&) noexcept;
    Result<std::size_t> send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
private:
    Status check() noexcept;
    DatagramIO* io_{}; const World* world_{}; AuthorityLease* lease_{}; AuthorityBindingConfig config_{};
    WorldIdentity world_identity_{};LeaseScope lease_scope_{};WorldAuthorityBinding* binding_{}; AuthorityWorkScope scope_{};
    std::thread::id owner_{}; bool busy_{},invalidated_{};
};
}
