#pragma once

#include "world.hpp"
#include <thread>

namespace superpos {

enum class ReplicaPhase : std::uint8_t { Empty, SpawnPending, Ready, Retiring };
struct ReplicaKey {
    std::uint16_t slot{};
    std::uint64_t incarnation{};
    Epoch authority_epoch{};
    Epoch connection_epoch{};
    Epoch replica_epoch{};
    Epoch encoding_epoch{};
    bool operator==(const ReplicaKey&) const noexcept = default;
};
struct BaselineImage { std::uint64_t token{}; std::uint64_t revision{}; bool pinned{}; };
struct ReplicaRecord {
    ReplicaPhase phase{ReplicaPhase::Empty};
    ObjectHandle handle{};
    PeerId owner{};
    const Schema* schema{};
    std::uint64_t incarnation{};
    Epoch encoding_epoch{1};
    std::uint64_t ownership_revision{};
    std::uint64_t spawn_sequence{};
    std::uint64_t leave_sequence{};
    std::uint64_t retire_sequence{};
    std::uint64_t last_sent_revision{};
    std::uint64_t initial_applied_revision{};
    std::uint64_t state_applied_revision{};
    std::uint64_t last_offer_milliseconds{};
    Epoch previous_encoding_epoch{};
    std::uint64_t reset_sequence{};
    BaselineImage images[2]{};
    std::int8_t active_image{-1};
    std::int8_t offered_image{-1};
    std::int8_t retiring_image{-1};
};
struct ReplicaConfig {
    Epoch authority_epoch{1};
    Epoch connection_epoch{1};
    Epoch replica_epoch{1};
    std::size_t maximum_active{1000};
    std::size_t maximum_transitions{64};
    std::size_t state_stride{64};
    std::uint64_t offer_interval_milliseconds{500};
    PeerId peer{};
    bool operator==(const ReplicaConfig&) const noexcept=default;
};
struct ReplicaBinding { ReplicaKey key{}; ObjectHandle handle{}; std::uint64_t lifecycle_sequence{}; };
struct BaselineOffer {
    ReplicaKey key{};
    std::uint64_t base_token{};
    std::uint64_t candidate_token{};
    std::uint64_t revision{};
    std::span<const std::byte> canonical;
};
struct BaselineRetirement {
    ReplicaKey key{};
    std::uint64_t old_token{};
    std::uint64_t replacement_token{};
    std::uint64_t lifecycle_sequence{};
};
struct ReplicaEncodingReset {
    ReplicaKey old_key{},new_key{}; PeerId owner{};
    std::uint64_t ownership_revision{},lifecycle_sequence{};
    bool operator==(const ReplicaEncodingReset&) const noexcept=default;
};

// Sender-side bounded lifecycle and baseline state. Transport and engine adapters
// supply authenticated receipts and readiness; this is not a network receiver.
// Caller storage is exclusive and outlives this owner-thread manager.
class PeerReplicas {
    std::thread::id owner_thread_{std::this_thread::get_id()};
    bool owned() const noexcept { return owner_thread_==std::this_thread::get_id(); }
    ReplicaConfig config_;
    std::span<ReplicaRecord> records_;
    std::span<std::byte> images_;
    std::size_t active_{};
    std::size_t ready_{};
    std::size_t transitions_{};
    std::uint64_t control_issued_{};
    std::uint64_t control_applied_{};
    std::uint64_t token_issued_{};
    std::uint64_t instance_{};
    Result<std::size_t> index(const ReplicaKey&) const noexcept;
    ReplicaKey key(std::size_t) const noexcept;
    std::span<std::byte> image(std::size_t, std::size_t) noexcept;
    std::span<const std::byte> image(std::size_t, std::size_t) const noexcept;
    void clear_images(std::size_t) noexcept;
    BaselineRetirement retirement(std::size_t) const noexcept;
    Result<ReplicaEncodingReset> issue_reset(const ReplicaKey&,PeerId,std::uint64_t) noexcept;
public:
    PeerReplicas() = default;
    PeerReplicas(const PeerReplicas&) = delete;
    PeerReplicas& operator=(const PeerReplicas&) = delete;
    PeerReplicas(PeerReplicas&&) noexcept;
    PeerReplicas& operator=(PeerReplicas&&) noexcept;
    static Result<PeerReplicas> create(ReplicaConfig, std::span<ReplicaRecord>, std::span<std::byte>) noexcept;
    Result<ReplicaBinding> begin_spawn(const EntityView&) noexcept;
    Status spawn_applied(const ReplicaKey&, std::uint64_t initial_revision, std::uint64_t ownership_revision) noexcept;
    Result<std::uint64_t> begin_retire(const ReplicaKey&) noexcept;
    Status control_applied(std::uint64_t cumulative_sequence) noexcept;
    Result<BaselineOffer> offer_baseline(const ReplicaKey&, std::span<const std::byte>,
        std::uint64_t revision, std::uint64_t monotonic_milliseconds) noexcept;
    Result<BaselineOffer> pending_offer(const ReplicaKey&) const noexcept;
    Result<BaselineRetirement> baseline_pinned(const ReplicaKey&, std::uint64_t token, std::uint64_t revision) noexcept;
    Result<std::span<const std::byte>> active_baseline(const ReplicaKey&, std::uint64_t token) const noexcept;
    Status note_state_sent(const ReplicaKey&, std::uint64_t revision) noexcept;
    Status state_applied(const ReplicaKey&, std::uint64_t revision) noexcept;
    // Exact ordered reset is issued by this same lifecycle sequence owner.
    // Another reset is Busy until the previous control prefix is Applied.
    Result<ReplicaEncodingReset> reset_encoding(const ReplicaKey&) noexcept;
    Result<ReplicaEncodingReset> ownership_changed(const ReplicaKey&, PeerId new_owner, std::uint64_t new_revision) noexcept;
    Result<ReplicaEncodingReset> pending_reset(const ReplicaKey& new_key) const noexcept;
    std::uint64_t lifecycle_issued() const noexcept { return owned()?control_issued_:0; }
    std::uint64_t lifecycle_applied() const noexcept { return owned()?control_applied_:0; }
    Result<ReplicaRecord> inspect(const ReplicaKey&) const noexcept;
    Result<ReplicaConfig> configuration() const noexcept;
    Result<std::uint64_t> instance_identity() const noexcept;
    Result<std::size_t> slot_capacity() const noexcept;
    // Owner-thread alias preflight; includes this manager and its exclusive
    // record/image pools, without exposing raw mutable storage.
    Result<bool> storage_overlaps(std::span<const std::byte>) const noexcept;
    std::size_t active_count() const noexcept { return owned()?active_:0; }
    std::size_t ready_count() const noexcept { return owned()?ready_:0; }
    std::size_t transition_count() const noexcept { return owned()?transitions_:0; }
};

}
