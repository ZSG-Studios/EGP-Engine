#pragma once
#include "capability.hpp"
#include "world.hpp"
#include <array>

namespace superpos {
enum class SchemaAuthority : std::uint8_t { ServerAuthoritative,OwnerAuthoritative };
enum class PublicationSemantics : std::uint8_t { PerObject,ExplicitAtomicGroup };
struct SchemaContract {
    std::uint64_t schema_version{1},codec_version{1};
    SchemaAuthority authority{SchemaAuthority::ServerAuthoritative};
    PublicationSemantics publication{PublicationSemantics::PerObject};
    std::uint64_t group_id{};
    std::uint32_t maximum_group_objects{1},maximum_group_bytes{65536},maximum_encoded_bytes{65536};
    bool operator==(const SchemaContract&) const noexcept=default;
};
struct SchemaRecord {
    bool occupied{}; Schema schema{}; SchemaContract contract{}; Fingerprint fingerprint{};
    std::array<FieldDescriptor,Schema::maximum_fields> fields{};
    std::array<RpcDescriptor,Schema::maximum_rpcs> rpcs{};
};
struct RegistryState { std::size_t count{}; std::uint64_t active_worlds{},revision{1}; };
class FrozenRegistry {
public:
    FrozenRegistry() noexcept=default;
    ~FrozenRegistry();
    FrozenRegistry(const FrozenRegistry&)=delete;
    FrozenRegistry& operator=(const FrozenRegistry&)=delete;
    FrozenRegistry(FrozenRegistry&&) noexcept;
    FrozenRegistry& operator=(FrozenRegistry&&) noexcept;
    Result<const SchemaRecord*> find(SchemaId) const noexcept;
    Status materialize(std::span<Schema>) const noexcept;
    std::size_t size() const noexcept { return records_.size(); }
    const Fingerprint& fingerprint() const noexcept { return fingerprint_; }
private:
    friend class SchemaRegistry;
    friend class WorldAuthorityBinding;
    FrozenRegistry(RegistryState& state,std::span<const SchemaRecord> records,Fingerprint fingerprint) noexcept:
        state_(&state),records_(records),fingerprint_(fingerprint) {}
    RegistryState* state_{}; std::span<const SchemaRecord> records_{}; Fingerprint fingerprint_{};
};
// Owner thread, exclusive caller storage, no allocation. Descriptors are copied
// so registration never depends on the original descriptor array's lifetime.
// Records/state outlive all frozen tokens and registered worlds. At most 256
// schemas, 64 fields/schema, 32 RPCs/schema. Append is forbidden while frozen.
class SchemaRegistry {
public:
    static constexpr std::size_t maximum_schemas=256,maximum_schema_encoding=8192;
    static constexpr std::size_t maximum_registry_encoding=8+2+maximum_schemas*40;
    static Result<SchemaRegistry> create(std::span<SchemaRecord>,RegistryState&,CryptographicDigest&) noexcept;
    SchemaRegistry(const SchemaRegistry&)=delete;
    SchemaRegistry& operator=(const SchemaRegistry&)=delete;
    SchemaRegistry(SchemaRegistry&&) noexcept;
    SchemaRegistry& operator=(SchemaRegistry&&) noexcept;
    Result<Fingerprint> add(const Schema&,SchemaContract,std::span<std::byte> scratch) noexcept;
    Result<FrozenRegistry> freeze(std::span<std::byte> scratch) noexcept;
    std::size_t size() const noexcept { return state_?state_->count:0; }
private:
    SchemaRegistry(std::span<SchemaRecord> records,RegistryState& state,CryptographicDigest& digest) noexcept:records_(records),state_(&state),digest_(&digest) {}
    std::span<SchemaRecord> records_{}; RegistryState* state_{}; CryptographicDigest* digest_{};
};
Result<std::size_t> encode_schema_contract(const Schema&,SchemaContract,std::span<std::byte>) noexcept;
// Own the freeze token for exactly the lifetime of this active World. The caller
// supplies a separate bounded array of Schema views; record storage stays stable.
class RegisteredWorld {
public:
    RegisteredWorld(const RegisteredWorld&)=delete;
    RegisteredWorld& operator=(const RegisteredWorld&)=delete;
    RegisteredWorld(RegisteredWorld&&) noexcept=default;
    RegisteredWorld& operator=(RegisteredWorld&&) noexcept=default;
    const World& world() const noexcept { return world_; }
    Result<ObjectHandle> spawn(SchemaId schema,PeerId owner,std::span<const std::byte> state,Tick tick) noexcept {
        if(!frozen_.size())return fail(Error::NotReady); return world_.spawn(schema,owner,state,tick);
    }
    Status destroy(ObjectHandle handle,Epoch epoch) noexcept {
        if(!frozen_.size())return fail(Error::NotReady); return world_.destroy(handle,epoch);
    }
    Result<std::uint64_t> publish(std::span<const Publication>,Tick,Epoch) noexcept;
    Result<std::uint64_t> transfer_ownership(ObjectHandle handle,PeerId owner,std::uint64_t revision,Tick tick,Epoch epoch) noexcept {
        if(!frozen_.size())return fail(Error::NotReady); return world_.transfer_ownership(handle,owner,revision,tick,epoch);
    }
    const Fingerprint& fingerprint() const noexcept { return frozen_.fingerprint(); }
private:
    friend class WorldAuthorityBinding;
    friend Result<RegisteredWorld> create_registered_world(SchemaRegistry&,std::span<std::byte>,WorldConfig,std::span<WorldSlot>,std::span<std::byte>,std::span<Schema>) noexcept;
    RegisteredWorld(World world,FrozenRegistry frozen) noexcept:world_(std::move(world)),frozen_(std::move(frozen)) {}
    World world_; FrozenRegistry frozen_;
};
Result<RegisteredWorld> create_registered_world(SchemaRegistry&,std::span<std::byte> fingerprint_scratch,
    WorldConfig,std::span<WorldSlot>,std::span<std::byte> state_arena,std::span<Schema> schema_views) noexcept;
} // namespace superpos
