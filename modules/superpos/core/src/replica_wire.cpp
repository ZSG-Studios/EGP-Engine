#include "superpos/replica_wire.hpp"
#include "superpos/codec.hpp"
#include <limits>
#include <type_traits>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
struct Measure {
    static constexpr bool reading=false;
    std::size_t size{3}; std::span<std::byte> destination{};
    Status add(std::size_t n) noexcept { if(n>ReplicaWireMessage::maximum_bytes-size)return fail(Error::CapacityExceeded); size+=n; return {}; }
    template<class T> Status value(const T& number) noexcept {
        auto v=static_cast<std::uint64_t>(number); std::size_t n=1; while(v>=128) { v>>=7; ++n; } return add(n);
    }
    Status data(std::span<const std::byte> bytes,std::size_t maximum) noexcept {
        if(bytes.empty() || bytes.size()>maximum)return fail(Error::CapacityExceeded);
        if(overlap(bytes,destination))return fail(Error::InvalidArgument);
        if(auto length=value(bytes.size());!length)return length; return add(bytes.size());
    }
};
struct Encode {
    static constexpr bool reading=false; Writer writer;
    template<class T> Status value(const T& number) noexcept { return writer.varuint(static_cast<std::uint64_t>(number)); }
    Status data(std::span<const std::byte> bytes,std::size_t) noexcept { if(auto length=value(bytes.size());!length)return length; return writer.raw(bytes); }
};
struct Decode {
    static constexpr bool reading=true; Reader reader;
    template<class T> Status value(T& number) noexcept {
        auto decoded=reader.varuint(); if(!decoded)return fail(decoded.error());
        if(*decoded>std::numeric_limits<T>::max())return fail(Error::Overflow); number=static_cast<T>(*decoded); return {};
    }
    Status data(std::span<const std::byte>& bytes,std::size_t maximum) noexcept {
        auto length=reader.varuint(); if(!length)return fail(length.error()); if(!*length || *length>maximum)return fail(Error::CapacityExceeded);
        auto payload=reader.raw(static_cast<std::size_t>(*length)); if(!payload)return fail(payload.error()); bytes=*payload; return {};
    }
};
template<class Archive,class... T> Status values(Archive& archive,T&... values) noexcept {
    Status status; ((status?status=archive.value(values):status),...); return status;
}
template<class Archive,class Key> Status identity(Archive& a,Key& key,const ReplicaWireContext& context) noexcept {
    if(auto s=values(a,key.slot,key.incarnation,key.encoding_epoch);!s)return s;
    if constexpr(Archive::reading) { key.authority_epoch=context.authority; key.connection_epoch=context.connection; key.replica_epoch=context.replica; }
    if(!key.slot || !key.incarnation || !key.encoding_epoch)return fail(Error::ProtocolViolation);
    if(key.authority_epoch!=context.authority || key.connection_epoch!=context.connection || key.replica_epoch!=context.replica)return fail(Error::StaleEpoch);
    return {};
}
template<class Archive,class Message> Status body(Archive& a,Message& m) noexcept {
    if(auto s=values(a,m.context.authority,m.context.connection,m.context.replica);!s)return s;
    if(!m.context.authority || !m.context.connection || !m.context.replica)return fail(Error::ProtocolViolation);
    switch(m.kind) {
    case ReplicaWireKind::Bind: {
        auto& b=m.binding; if(auto s=identity(a,b.key,m.context);!s)return s;
        if(auto s=values(a,b.handle.value,b.schema,b.owner,b.ownership_revision,b.lifecycle_sequence);!s)return s;
        if(!b.handle || !b.schema || !b.ownership_revision || !b.lifecycle_sequence)return fail(Error::ProtocolViolation); break;
    }
    case ReplicaWireKind::BaselineOffer: {
        auto& b=m.baseline; if(auto s=identity(a,b.key,m.context);!s)return s;
        if(auto s=values(a,b.base_token,b.candidate_token,b.revision,m.sequence,m.tick);!s)return s;
        if(!b.candidate_token || !b.revision || !m.sequence || (b.base_token && b.candidate_token<=b.base_token))return fail(Error::ProtocolViolation);
        if(auto s=a.data(b.canonical,Schema::maximum_state_bytes);!s)return s; break;
    }
    case ReplicaWireKind::SpawnApplied:
        if(auto s=identity(a,m.spawned.key,m.context);!s)return s;
        if(auto s=values(a,m.spawned.revision,m.spawned.ownership_revision);!s)return s;
        if(!m.spawned.revision || !m.spawned.ownership_revision)return fail(Error::ProtocolViolation); break;
    case ReplicaWireKind::BaselinePinned:
        if(auto s=identity(a,m.pinned.key,m.context);!s)return s;
        if(auto s=values(a,m.pinned.token,m.pinned.revision);!s)return s;
        if(!m.pinned.token || !m.pinned.revision)return fail(Error::ProtocolViolation); break;
    case ReplicaWireKind::BaselineRetire: {
        auto& r=m.retirement; if(auto s=identity(a,r.key,m.context);!s)return s;
        if(auto s=values(a,r.old_token,r.replacement_token,r.lifecycle_sequence);!s)return s;
        if(!r.old_token || r.replacement_token<=r.old_token || !r.lifecycle_sequence)return fail(Error::ProtocolViolation); break;
    }
    case ReplicaWireKind::LifecycleApplied:
        if(auto s=a.value(m.sequence);!s)return s; if(!m.sequence)return fail(Error::ProtocolViolation); break;
    case ReplicaWireKind::Leave:
        if(auto s=identity(a,m.key,m.context);!s)return s;
        if(auto s=a.value(m.sequence);!s)return s; if(!m.sequence)return fail(Error::ProtocolViolation); break;
    case ReplicaWireKind::OwnershipReset:
        if(auto s=identity(a,m.key,m.context);!s)return s;
        if(auto s=identity(a,m.new_key,m.context);!s)return s;
        if(auto s=values(a,m.owner,m.ownership_revision,m.sequence);!s)return s;
        if(!m.ownership_revision || !m.sequence)return fail(Error::ProtocolViolation);
        if(m.key.encoding_epoch==UINT64_MAX)return fail(Error::CounterExhausted);
        if(m.new_key.slot!=m.key.slot || m.new_key.incarnation!=m.key.incarnation || m.new_key.encoding_epoch!=m.key.encoding_epoch+1)return fail(Error::ProtocolViolation); break;
    case ReplicaWireKind::Publication:
    case ReplicaWireKind::FullRepair: {
        if(auto s=values(a,m.group_id,m.tick,m.revision,m.sequence,m.count);!s)return s;
        if(!m.revision || !m.sequence || !m.count || m.count>m.maximum_members || (!m.group_id && m.count!=1))return fail(Error::ProtocolViolation);
        std::uint16_t previous{};
        for(std::size_t n=0;n<m.count;++n) {
            if(m.kind==ReplicaWireKind::Publication) {
                auto& p=m.patches[n]; if(auto s=identity(a,p.key,m.context);!s)return s;
                if(p.key.slot<=previous)return fail(Error::NonCanonical); previous=p.key.slot;
                if constexpr(Archive::reading)p.revision=m.revision;
                if(p.revision!=m.revision)return fail(Error::ProtocolViolation);
                if(auto s=a.value(p.baseline_token);!s)return s; if(!p.baseline_token)return fail(Error::ProtocolViolation);
                if(auto s=a.data(p.delta,m.maximum_bytes);!s)return s;
            } else {
                auto& p=m.repairs[n]; if(auto s=identity(a,p.key,m.context);!s)return s;
                if(p.key.slot<=previous)return fail(Error::NonCanonical); previous=p.key.slot;
                if constexpr(Archive::reading)p.revision=m.revision;
                if(p.revision!=m.revision)return fail(Error::ProtocolViolation);
                if(auto s=a.data(p.canonical,Schema::maximum_state_bytes);!s)return s;
            }
        } break;
    }
    case ReplicaWireKind::StateApplied:
    case ReplicaWireKind::RepairRequest: {
        if(auto s=a.value(m.count);!s)return s;
        const auto maximum=m.kind==ReplicaWireKind::StateApplied?m.maximum_receipts:m.maximum_members;
        if(!m.count || m.count>maximum)return fail(Error::ProtocolViolation);
        std::uint16_t previous{};
        for(std::size_t n=0;n<m.count;++n) {
            if(m.kind==ReplicaWireKind::StateApplied) {
                auto& r=m.applied[n]; if(auto s=identity(a,r.key,m.context);!s)return s;
                if(r.key.slot<=previous)return fail(Error::NonCanonical); previous=r.key.slot;
                if(auto s=a.value(r.revision);!s)return s; if(!r.revision)return fail(Error::ProtocolViolation);
            } else {
                auto& r=m.requested[n]; if(auto s=identity(a,r.key,m.context);!s)return s;
                if(r.key.slot<=previous)return fail(Error::NonCanonical); previous=r.key.slot;
                if(auto s=values(a,r.missing_token,r.last_applied_revision);!s)return s;
            }
        } break;
    }
    default:return fail(Error::Unsupported);
    } return {};
}
}
Result<std::size_t> encode_replica_message(const ReplicaWireMessage& message,std::span<std::byte> output) noexcept {
    if(overlap(std::as_bytes(std::span(&message,1)),output))return fail(Error::InvalidArgument);
    Measure measure; measure.destination=output;
    if(auto valid=body(measure,message);!valid)return fail(valid.error());
    if(output.size()<measure.size)return fail(Error::CapacityExceeded);
    Encode archive{Writer(output.first(measure.size))};
    const std::array header{std::byte{0x53},std::byte{1},std::byte(static_cast<unsigned>(message.kind))};
    if(auto prefix=archive.writer.raw(header);!prefix)return fail(prefix.error());
    if(auto encoded=body(archive,message);!encoded)return fail(encoded.error()); return archive.writer.size();
}
Result<ReplicaWireMessage> decode_replica_message(std::span<const std::byte> input) noexcept {
    if(input.size()>ReplicaWireMessage::maximum_bytes)return fail(Error::CapacityExceeded);
    Decode archive{Reader(input)}; auto header=archive.reader.raw(3); if(!header)return fail(header.error());
    if((*header)[0]!=std::byte{0x53} || (*header)[1]!=std::byte{1})return fail(Error::Unsupported);
    ReplicaWireMessage message; message.kind=static_cast<ReplicaWireKind>(std::to_integer<unsigned>((*header)[2]));
    if(auto decoded=body(archive,message);!decoded)return fail(decoded.error()); if(!archive.reader.empty())return fail(Error::NonCanonical); return message;
}
Result<ReplicaLane> replica_lane(ReplicaWireKind kind) noexcept {
    switch(kind) {
    case ReplicaWireKind::Publication:return ReplicaLane::State;
    case ReplicaWireKind::BaselineOffer:case ReplicaWireKind::FullRepair:return ReplicaLane::Bulk;
    case ReplicaWireKind::Bind:case ReplicaWireKind::SpawnApplied:case ReplicaWireKind::BaselinePinned:
    case ReplicaWireKind::BaselineRetire:case ReplicaWireKind::LifecycleApplied:case ReplicaWireKind::Leave:
    case ReplicaWireKind::OwnershipReset:case ReplicaWireKind::StateApplied:case ReplicaWireKind::RepairRequest:return ReplicaLane::Control;
    default:return fail(Error::Unsupported);
    }
}
Status validate_replica_delivery(ReplicaWireKind kind,ReplicaLane lane,DeliveryMode mode) noexcept {
    auto required=replica_lane(kind); if(!required)return fail(required.error()); if(*required!=lane)return fail(Error::ProtocolViolation);
    const bool allowed=lane==ReplicaLane::Control?mode==DeliveryMode::ReliableOrdered:
        lane==ReplicaLane::Bulk?mode==DeliveryMode::ReliableUnordered:
        mode==DeliveryMode::ReliableUnordered || mode==DeliveryMode::Unreliable;
    return allowed?Status{}:Status{fail(Error::ProtocolViolation)};
}
}
