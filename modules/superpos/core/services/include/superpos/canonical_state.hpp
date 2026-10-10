// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/checkpoint.hpp"

namespace superpos {
inline constexpr std::size_t canonical_state_header_bytes=256;
inline constexpr std::size_t canonical_state_record_bytes=1024*1024;
enum class CanonicalStateKind : std::uint8_t { Genesis,Checkpoint,FullRecord,DeltaRecord };
struct CanonicalStatePosition {
    std::uint64_t match{};Epoch epoch{};std::uint64_t sequence{};Tick tick{};
    bool operator==(const CanonicalStatePosition&) const noexcept=default;
};
struct CanonicalStateHeader {
    CanonicalStateKind kind{};
    CanonicalStatePosition position{};
    Fingerprint schemas{},simulation{},participants{},codec{};
    bool has_predecessor{};
    Fingerprint predecessor{},result{};
    bool operator==(const CanonicalStateHeader&) const noexcept=default;
};
// Position is deliberately absent: the store assigns it under its write lock.
// The bootstrap predecessor is a qualified producer's genesis claim.
struct CanonicalStateClaim {
    CanonicalStateKind kind{CanonicalStateKind::FullRecord};
    Fingerprint schemas{},simulation{},participants{},codec{};
    Fingerprint predecessor{},result{};
};
struct CanonicalStateView {
    CanonicalStateHeader header{};
    std::span<const std::byte> payload{};
};
// Claims emitted by a trusted qualified producer. These helpers provide exact
// canonical encoding, NOT authentication or proof of semantic state coverage.
// Store the entire envelope as checkpoint content / journal canonical bytes.
// The containing committed receipt or authenticated manifest supplies trust.
// Payload and output must not overlap; immutable payload bytes are borrowed.
Result<std::size_t> encode_canonical_state(const CanonicalStateHeader&,
    std::span<const std::byte> payload,std::span<std::byte> output) noexcept;
Result<CanonicalStateView> decode_canonical_state(std::span<const std::byte>) noexcept;
// Header of an envelope whose complete length is known but whose payload is not
// in memory (for example, the first stored checkpoint chunk). The declared
// payload length must equal total-256; prefix may hold only leading bytes.
Result<CanonicalStateHeader> decode_canonical_state_header(
    std::span<const std::byte> prefix,std::uint64_t total_bytes) noexcept;

// Metadata/digest continuity only. It never applies deltas or validates their
// meaning. Anchors are explicit full checkpoints; journal records always bind
// the preceding complete-state digest, including full replacement records.
class CanonicalStateChain {
    CanonicalStateHeader current_{};
    explicit CanonicalStateChain(CanonicalStateHeader h) noexcept:current_(h){}
public:
    static Result<CanonicalStateChain> create(const CanonicalStateHeader& anchor) noexcept;
    Status admit(const CanonicalStateHeader&) noexcept;
    Status finish(CanonicalStatePosition,const Fingerprint& final_state) const noexcept;
    const CanonicalStateHeader& current() const noexcept{return current_;}
};
}
