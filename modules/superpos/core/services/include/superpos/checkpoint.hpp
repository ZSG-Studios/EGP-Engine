// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/capability.hpp"

namespace superpos {
inline constexpr std::uint32_t checkpoint_chunk_bytes=64*1024;
inline constexpr std::uint32_t checkpoint_maximum_chunks=768;
inline constexpr std::uint64_t checkpoint_maximum_bytes=48ULL*1024*1024;
// Version 1 content container. These declarations bind the supplied bytes, not
// proof that the application captured every required participant/service table.
struct CheckpointManifest {
    std::uint64_t match{},id{};
    Epoch epoch{};
    std::uint64_t sequence{};
    Tick tick{};
    std::uint64_t bytes{};
    Fingerprint schemas{},simulation{},participants{};
    // Fixed service format identity, known before capture. The later captured
    // state commitment is CheckpointServiceProgress::commitment, not this field.
    Fingerprint service_codec{},content{};
    RecoveryGrade grade{RecoveryGrade::None};
    bool operator==(const CheckpointManifest&) const noexcept=default;
};
struct CheckpointTicket {
    std::uint64_t match{},id{},generation{};
    bool operator==(const CheckpointTicket&) const noexcept=default;
};
enum class CheckpointContentState : std::uint8_t { Staging,ContentVerified,RecoveryVerified };
struct CheckpointContentReceipt {
    CheckpointTicket ticket{};
    CheckpointManifest manifest{};
    CheckpointContentState state{};
};
// Compute the versioned domain-separated manifest/chunk commitment. This is
// deliberately not SHA-256(concatenated image). Chunks use SHA-256 individually.
Result<Fingerprint> checkpoint_service_codec(CryptographicDigest&) noexcept;
Result<Fingerprint> checkpoint_content_commitment(const CheckpointManifest&,
    std::span<const Fingerprint> chunks,CryptographicDigest&) noexcept;

class CheckpointContentReader {
public:
    virtual ~CheckpointContentReader()=default;
    virtual Result<std::size_t> read(std::uint32_t chunk,std::span<std::byte>) noexcept=0;
};
// Trusted native codec validation of an immutable content image. Runs on the
// storage owner, must remain bounded, must not retain the reader, mutate/reenter the journal, and
// must not touch engine objects. Engine participant preparation belongs on the
// engine owner before capture. Passing this validator alone does not establish
// complete service-state capture, recovery fencing, or publish permission.
class CheckpointParticipantVerifier {
public:
    virtual ~CheckpointParticipantVerifier()=default;
    // Covers exact participant codecs and, for local replay, build/ABI configuration.
    virtual Fingerprint participant_fingerprint() const noexcept=0;
    virtual RecoveryGrade supported_grade() const noexcept=0;
    virtual Status verify(const CheckpointManifest&,CheckpointContentReader&) noexcept=0;
};
struct CheckpointServiceProgress {
    CheckpointTicket ticket{};
    std::uint64_t revision{},bytes{},cache_order{};
    std::uint32_t pages{},next_table{};
    bool complete{};
    // Store-generated state hash binds this revision, the encoded pages, boot
    // identity, source anchor and cache-order high-water. No circular dependency
    // on this hash exists when constructing the participant content manifest.
    Fingerprint commitment{};
};
// Validated host checkpoint within the retained coordinator ordering domain.
// This is not a standalone SQLite backup: append identities and the journal
// remain in that domain. Compaction (SqliteJournal::compact_journal) prunes
// only behind the oldest of two such bases and keeps compacted identities.
struct CheckpointServiceOrigin {
    // Historical seed binding only. It never establishes a current boot/lease.
    std::uint64_t boot_term{};
    Fingerprint boot_nonce{};
    bool operator==(const CheckpointServiceOrigin&) const noexcept=default;
};
struct CheckpointRecoveryReceipt {
    CheckpointContentReceipt content{};
    CheckpointServiceProgress service{};
    CheckpointServiceOrigin origin{};
};
// Replacement IDs are persistent per match. Exact request replay is idempotent;
// IDs cannot be reused even after both referenced images have been retired.
struct CheckpointReplacementRequest {
    std::uint64_t operation_epoch{},operation{};
    CheckpointTicket candidate{},retiring{};
    bool operator==(const CheckpointReplacementRequest&) const noexcept=default;
};
enum class CheckpointReplacementState : std::uint8_t { Scanning,Committed,Cancelled,Retired };
struct CheckpointReplacementProgress {
    CheckpointReplacementRequest request{};
    CheckpointTicket survivor{};
    std::uint64_t revision{},scanned_through{},target{},records{},bytes{};
    // Coverage completion only. A cache-expired Retired result carries no
    // assertion that this particular payload was committed or ever validated.
    bool complete{};
    Fingerprint commitment{};
    CheckpointReplacementState state{CheckpointReplacementState::Scanning};
};
// Structural coverage only: exact contiguous records and valid decision/effect
// envelopes. This is not simulation replay or cross-epoch migration evidence.
inline constexpr std::uint32_t checkpoint_coverage_page_bytes=1024*1024+56;
inline constexpr std::uint32_t checkpoint_coverage_page_records=64;
inline constexpr std::uint32_t checkpoint_replacement_cache=64;
struct CheckpointParticipantReceipt {
    CheckpointContentReceipt content{};
    // Process-local validation only; never an authoritative recovery receipt.
    RecoveryGrade validated_grade{RecoveryGrade::None};
};
}
