// SPDX-License-Identifier: MIT
#include "superpos/canonical_state.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <limits>

namespace superpos {
namespace {
constexpr std::uint64_t magic=0x53505343414e0001ULL;
Status validate(const CanonicalStateHeader& h) noexcept {
    if(static_cast<unsigned>(h.kind)>3)return fail(Error::Unsupported);
    if(!h.position.match||!h.position.epoch||!known_fingerprint(h.schemas)||!known_fingerprint(h.simulation)||
       !known_fingerprint(h.participants)||!known_fingerprint(h.codec)||!known_fingerprint(h.result))return fail(Error::InvalidArgument);
    if(h.kind==CanonicalStateKind::Genesis||h.kind==CanonicalStateKind::Checkpoint){if(h.has_predecessor||known_fingerprint(h.predecessor)||(h.kind==CanonicalStateKind::Genesis)!=(h.position.sequence==0))return fail(Error::NonCanonical);}
    else if(!h.position.sequence||!h.has_predecessor||!known_fingerprint(h.predecessor))return fail(Error::NonCanonical);
    return {};
}
std::size_t limit(CanonicalStateKind kind) noexcept {return kind==CanonicalStateKind::Genesis||kind==CanonicalStateKind::Checkpoint?checkpoint_maximum_bytes:canonical_state_record_bytes;}
}
Result<std::size_t> encode_canonical_state(const CanonicalStateHeader& supplied,std::span<const std::byte> payload,std::span<std::byte> output) noexcept {
    const auto h=supplied;if(auto r=validate(h);!r)return fail(r.error());
    if(payload.size()>limit(h.kind)-canonical_state_header_bytes)return fail(Error::CapacityExceeded);
    const auto total=canonical_state_header_bytes+payload.size();if(output.size()<total)return fail(Error::Truncated);
    Writer w(output.first(total));for(auto n:{magic,std::uint64_t(h.kind),h.position.match,h.position.epoch,h.position.sequence,h.position.tick})if(auto r=w.u64(n);!r)return fail(r.error());
    for(const auto* f:{&h.schemas,&h.simulation,&h.participants,&h.codec})if(auto r=w.raw(*f);!r)return fail(r.error());
    if(auto r=w.u64(h.has_predecessor?1:0);!r)return fail(r.error());
    for(const auto* f:{&h.predecessor,&h.result})if(auto r=w.raw(*f);!r)return fail(r.error());
    if(auto r=w.u64(payload.size());!r)return fail(r.error());if(auto r=w.raw(payload);!r)return fail(r.error());return w.size();
}
namespace {
// Parses exactly the fixed header and returns its declared payload length.
Result<std::uint64_t> parse_header(std::span<const std::byte> bytes,CanonicalStateHeader& h) noexcept {
    if(bytes.size()<canonical_state_header_bytes)return fail(Error::Truncated);
    Reader r(bytes.first(canonical_state_header_bytes));auto marker=r.u64(),kind=r.u64(),match=r.u64(),epoch=r.u64(),sequence=r.u64(),tick=r.u64();
    if(!marker||!kind||!match||!epoch||!sequence||!tick)return fail(Error::Truncated);
    if(*marker!=magic||*kind>3)return fail(Error::Unsupported);
    h.kind=static_cast<CanonicalStateKind>(*kind);h.position={*match,*epoch,*sequence,*tick};
    for(auto* f:{&h.schemas,&h.simulation,&h.participants,&h.codec}){auto data=r.raw(32);if(!data)return fail(data.error());std::copy(data->begin(),data->end(),f->begin());}
    auto presence=r.u64();if(!presence)return fail(presence.error());if(*presence>1)return fail(Error::NonCanonical);h.has_predecessor=bool(*presence);
    for(auto* f:{&h.predecessor,&h.result}){auto data=r.raw(32);if(!data)return fail(data.error());std::copy(data->begin(),data->end(),f->begin());}
    auto size=r.u64();if(!size)return fail(size.error());if(*size>limit(h.kind)-canonical_state_header_bytes)return fail(Error::CapacityExceeded);
    return *size;
}
}
Result<CanonicalStateView> decode_canonical_state(std::span<const std::byte> bytes) noexcept {
    CanonicalStateView result;auto size=parse_header(bytes,result.header);if(!size)return fail(size.error());
    const auto rest=bytes.subspan(canonical_state_header_bytes);if(rest.size()<*size)return fail(Error::Truncated);if(rest.size()!=*size)return fail(Error::NonCanonical);
    if(auto valid=validate(result.header);!valid)return fail(valid.error());result.payload=rest;return result;
}
Result<CanonicalStateHeader> decode_canonical_state_header(std::span<const std::byte> prefix,std::uint64_t total) noexcept {
    CanonicalStateHeader header;auto size=parse_header(prefix,header);if(!size)return fail(size.error());
    if(total<canonical_state_header_bytes||total-canonical_state_header_bytes!=*size)return fail(Error::NonCanonical);
    if(prefix.size()>total)return fail(Error::NonCanonical);
    if(auto valid=validate(header);!valid)return fail(valid.error());return header;
}
Result<CanonicalStateChain> CanonicalStateChain::create(const CanonicalStateHeader& h) noexcept {
    if(auto r=validate(h);!r)return fail(r.error());if(h.kind!=CanonicalStateKind::Genesis&&h.kind!=CanonicalStateKind::Checkpoint)return fail(Error::InvalidArgument);return CanonicalStateChain(h);
}
Status CanonicalStateChain::admit(const CanonicalStateHeader& h) noexcept {
    if(auto r=validate(h);!r)return r;if(h.kind==CanonicalStateKind::Genesis||h.kind==CanonicalStateKind::Checkpoint)return fail(Error::InvalidArgument);
    if(h.position.match!=current_.position.match||h.schemas!=current_.schemas||h.simulation!=current_.simulation||h.participants!=current_.participants||h.codec!=current_.codec)return fail(Error::IncompatibleSchema);
    if(current_.position.sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    if(h.position.sequence!=current_.position.sequence+1||h.position.tick<current_.position.tick||h.position.epoch<current_.position.epoch||h.predecessor!=current_.result)return fail(Error::RecoveryUnavailable);
    current_=h;return {};
}
Status CanonicalStateChain::finish(CanonicalStatePosition position,const Fingerprint& digest) const noexcept {
    if(position!=current_.position||digest!=current_.result)return fail(Error::RecoveryUnavailable);return {};
}
}
