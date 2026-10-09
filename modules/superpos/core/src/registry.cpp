#include "superpos/registry.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false; auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
Status valid_contract(const Schema& schema,SchemaContract c) noexcept {
    auto checked=Schema::create(schema.id(),schema.fields(),schema.rpcs());
    if(!checked)return fail(checked.error()); if(checked->state_bytes()!=schema.state_bytes())return fail(Error::IncompatibleSchema);
    if(!c.schema_version || c.codec_version!=1 || c.authority!=SchemaAuthority::ServerAuthoritative)return fail(Error::Unsupported);
    if(static_cast<unsigned>(c.publication)>1 || !c.maximum_group_objects || c.maximum_group_objects>World::maximum_group_objects ||
        c.maximum_group_bytes<schema.state_bytes() || c.maximum_group_bytes>World::maximum_group_bytes ||
        c.maximum_encoded_bytes<schema.state_bytes()+8 || c.maximum_encoded_bytes>65536)return fail(Error::InvalidArgument);
    if(c.publication==PublicationSemantics::PerObject && (c.group_id || c.maximum_group_objects!=1))return fail(Error::InvalidArgument);
    if(c.publication==PublicationSemantics::ExplicitAtomicGroup && !c.group_id)return fail(Error::InvalidArgument);
    return {};
}
std::uint64_t canonical_double(double value) noexcept { return value==0?0:std::bit_cast<std::uint64_t>(value); }
}
Result<std::size_t> encode_schema_contract(const Schema& schema,SchemaContract c,std::span<std::byte> output) noexcept {
    if(auto valid=valid_contract(schema,c);!valid)return fail(valid.error());
    std::array<std::size_t,Schema::maximum_fields> fields{}; std::array<std::size_t,Schema::maximum_rpcs> rpcs{};
    for(std::size_t i=0;i<schema.fields().size();++i)fields[i]=i;
    for(std::size_t i=0;i<schema.rpcs().size();++i)rpcs[i]=i;
    std::sort(fields.begin(),fields.begin()+schema.fields().size(),[&](auto a,auto b){return schema.fields()[a].id<schema.fields()[b].id;});
    std::sort(rpcs.begin(),rpcs.begin()+schema.rpcs().size(),[&](auto a,auto b){return schema.rpcs()[a].id<schema.rpcs()[b].id;});
    std::array<std::byte,SchemaRegistry::maximum_schema_encoding> staged{}; Writer w(staged);
    if(!w.u64(0x5350534348454d01ULL) || !w.u64(schema.id()) || !w.u64(c.schema_version) || !w.u64(c.codec_version) ||
        !w.varuint(static_cast<unsigned>(c.authority)) || !w.varuint(static_cast<unsigned>(c.publication)) || !w.u64(c.group_id) ||
        !w.varuint(c.maximum_group_objects) || !w.varuint(c.maximum_group_bytes) || !w.varuint(c.maximum_encoded_bytes) ||
        !w.varuint(schema.state_bytes()) || !w.varuint(schema.fields().size()))return fail(Error::Overflow);
    for(std::size_t i=0;i<schema.fields().size();++i) {
        const auto& f=schema.fields()[fields[i]];
        if(!w.u64(f.id) || !w.varuint(static_cast<unsigned>(f.kind)) || !w.varuint(f.offset) || !w.varuint(f.size) || !w.varuint(static_cast<unsigned>(f.audience)) ||
            !w.u64(canonical_double(f.minimum)) || !w.u64(canonical_double(f.maximum)) || !w.varuint(f.quantization_levels))return fail(Error::Overflow);
    }
    if(!w.varuint(schema.rpcs().size()))return fail(Error::Overflow);
    for(std::size_t i=0;i<schema.rpcs().size();++i) {
        const auto& r=schema.rpcs()[rpcs[i]];
        if(!w.u64(r.id) || !w.varuint(static_cast<unsigned>(r.permission)) || !w.varuint(r.maximum_payload))return fail(Error::Overflow);
    }
    if(output.size()<w.size())return fail(Error::Truncated);
    std::memmove(output.data(),staged.data(),w.size()); return w.size();
}
FrozenRegistry::~FrozenRegistry() { if(state_ && state_->active_worlds)--state_->active_worlds; }
FrozenRegistry::FrozenRegistry(FrozenRegistry&& other) noexcept:
    state_(std::exchange(other.state_,nullptr)),records_(std::exchange(other.records_,{})),fingerprint_(other.fingerprint_) {}
FrozenRegistry& FrozenRegistry::operator=(FrozenRegistry&& other) noexcept {
    if(this!=&other) { if(state_ && state_->active_worlds)--state_->active_worlds; state_=std::exchange(other.state_,nullptr); records_=std::exchange(other.records_,{}); fingerprint_=other.fingerprint_; } return *this;
}
Result<const SchemaRecord*> FrozenRegistry::find(SchemaId id) const noexcept {
    if(!state_)return fail(Error::NotReady);
    for(const auto& r:records_)if(r.schema.id()==id)return &r; return fail(Error::IncompatibleSchema);
}
Status FrozenRegistry::materialize(std::span<Schema> output) const noexcept {
    if(!state_)return fail(Error::NotReady); if(output.size()<records_.size())return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(output),std::as_bytes(records_)))return fail(Error::InvalidArgument);
    for(std::size_t i=0;i<records_.size();++i)output[i]=records_[i].schema; return {};
}
SchemaRegistry::SchemaRegistry(SchemaRegistry&& other) noexcept:
    records_(std::exchange(other.records_,{})),state_(std::exchange(other.state_,nullptr)),digest_(std::exchange(other.digest_,nullptr)) {}
SchemaRegistry& SchemaRegistry::operator=(SchemaRegistry&& other) noexcept {
    if(this!=&other) { records_=std::exchange(other.records_,{}); state_=std::exchange(other.state_,nullptr); digest_=std::exchange(other.digest_,nullptr); } return *this;
}
Result<SchemaRegistry> SchemaRegistry::create(std::span<SchemaRecord> records,RegistryState& state,CryptographicDigest& digest) noexcept {
    if(records.empty() || records.size()>maximum_schemas)return fail(Error::InvalidArgument);
    if(state.active_worlds)return fail(Error::Busy); if(digest.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    if(overlap(std::as_bytes(records),std::as_bytes(std::span(&state,1))))return fail(Error::InvalidArgument);
    for(auto& r:records)r={}; state={}; return SchemaRegistry(records,state,digest);
}
Result<Fingerprint> SchemaRegistry::add(const Schema& source,SchemaContract contract,std::span<std::byte> scratch) noexcept {
    if(!state_)return fail(Error::NotReady); if(state_->active_worlds)return fail(Error::Busy);
    if(overlap(scratch,std::as_bytes(records_)) || overlap(scratch,std::as_bytes(std::span(state_,1))))return fail(Error::InvalidArgument);
    if(state_->revision==UINT64_MAX)return fail(Error::CounterExhausted);
    if(auto valid=valid_contract(source,contract);!valid)return fail(valid.error());
    for(std::size_t i=0;i<state_->count;++i) {
        const auto& old=records_[i];
        if(contract.group_id && old.contract.group_id==contract.group_id &&
            (old.contract.maximum_group_objects!=contract.maximum_group_objects || old.contract.maximum_group_bytes!=contract.maximum_group_bytes))return fail(Error::IncompatibleSchema);
    }
    std::array<FieldDescriptor,Schema::maximum_fields> fields{}; std::array<RpcDescriptor,Schema::maximum_rpcs> rpcs{};
    std::copy(source.fields().begin(),source.fields().end(),fields.begin()); std::copy(source.rpcs().begin(),source.rpcs().end(),rpcs.begin());
    std::sort(rpcs.begin(),rpcs.begin()+source.rpcs().size(),[](const auto& a,const auto& b){return a.id<b.id;});
    auto checked=Schema::create(source.id(),std::span(fields).first(source.fields().size()),std::span(rpcs).first(source.rpcs().size())); if(!checked)return fail(checked.error());
    auto length=encode_schema_contract(*checked,contract,scratch); if(!length)return fail(length.error());
    Fingerprint fingerprint{}; if(auto hashed=digest_->hash(scratch.first(*length),fingerprint);!hashed)return fail(hashed.error());
    if(!known_fingerprint(fingerprint))return fail(Error::AuthenticationFailed);
    for(std::size_t i=0;i<state_->count;++i) if(records_[i].schema.id()==source.id()) {
        if(records_[i].fingerprint!=fingerprint || records_[i].contract!=contract || !records_[i].schema.compatible_with(*checked))return fail(Error::IncompatibleSchema); return fingerprint;
    }
    if(state_->count==records_.size())return fail(Error::CapacityExceeded);
    auto& record=records_[state_->count]; record.fields=fields; record.rpcs=rpcs;
    auto schema=Schema::create(source.id(),std::span(record.fields).first(source.fields().size()),std::span(record.rpcs).first(source.rpcs().size()));
    if(!schema)return fail(schema.error()); // Validated identical copies, unreachable for a conforming owner thread.
    record.schema=*schema; record.contract=contract; record.fingerprint=fingerprint; record.occupied=true;
    ++state_->count; ++state_->revision; return fingerprint;
}
Result<FrozenRegistry> SchemaRegistry::freeze(std::span<std::byte> scratch) noexcept {
    if(!state_ || !state_->count)return fail(Error::NotReady); if(state_->active_worlds==UINT64_MAX)return fail(Error::CounterExhausted);
    if(overlap(scratch,std::as_bytes(records_)) || overlap(scratch,std::as_bytes(std::span(state_,1))))return fail(Error::InvalidArgument);
    std::array<std::size_t,maximum_schemas> order{}; for(std::size_t i=0;i<state_->count;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+state_->count,[&](auto a,auto b){return records_[a].schema.id()<records_[b].schema.id();});
    const auto required=8+2+state_->count*40; if(scratch.size()<required)return fail(Error::CapacityExceeded);
    Writer w(scratch); if(!w.u64(0x5350524547495301ULL) || !w.varuint(state_->count))return fail(Error::Truncated);
    for(std::size_t i=0;i<state_->count;++i) { const auto& r=records_[order[i]]; if(!w.u64(r.schema.id()) || !w.raw(r.fingerprint))return fail(Error::Truncated); }
    Fingerprint fingerprint{}; if(auto hash=digest_->hash(scratch.first(w.size()),fingerprint);!hash)return fail(hash.error());
    if(!known_fingerprint(fingerprint))return fail(Error::AuthenticationFailed);
    ++state_->active_worlds; return FrozenRegistry(*state_,std::span<const SchemaRecord>(records_).first(state_->count),fingerprint);
}
Result<std::uint64_t> RegisteredWorld::publish(std::span<const Publication> updates,Tick tick,Epoch epoch) noexcept {
    if(!frozen_.size())return fail(Error::NotReady);
    if(updates.empty() || updates.size()>World::maximum_group_objects)return fail(Error::InvalidArgument);
    std::uint64_t group{}; std::size_t bytes{};
    for(const auto& update:updates) {
        auto object=world_.view(update.handle); if(!object)return fail(object.error()); auto schema=frozen_.find(object->schema->id()); if(!schema)return fail(schema.error());
        if(update.canonical.size()!=object->canonical.size())return fail(Error::IncompatibleSchema);
        const auto& contract=(*schema)->contract; bytes+=update.canonical.size();
        if(updates.size()>contract.maximum_group_objects || bytes>contract.maximum_group_bytes)return fail(Error::CapacityExceeded);
        if(updates.size()>1) {
            if(contract.publication!=PublicationSemantics::ExplicitAtomicGroup)return fail(Error::Unsupported);
            if(!group)group=contract.group_id; else if(group!=contract.group_id)return fail(Error::IncompatibleSchema);
        }
    } return world_.publish(updates,tick,epoch);
}
Result<RegisteredWorld> create_registered_world(SchemaRegistry& registry,std::span<std::byte> scratch,WorldConfig config,
    std::span<WorldSlot> slots,std::span<std::byte> arena,std::span<Schema> views) noexcept {
    if(overlap(scratch,arena) || overlap(scratch,std::as_bytes(slots)) || overlap(scratch,std::as_bytes(views)))return fail(Error::InvalidArgument);
    auto frozen=registry.freeze(scratch); if(!frozen)return fail(frozen.error());
    if(auto copied=frozen->materialize(views);!copied)return fail(copied.error());
    auto world=World::create(config,slots,arena,std::span<const Schema>(views).first(frozen->size())); if(!world)return fail(world.error());
    return RegisteredWorld(std::move(*world),std::move(*frozen));
}
} // namespace superpos
