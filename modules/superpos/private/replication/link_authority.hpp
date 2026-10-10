// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 authority-side replication owner for ONE link Session. It
// drives the core PeerReplicas sender store and ReplicaAuthoritySession
// bridge, so delivery, retransmission, receipts and repair requests follow
// core semantics. Every pool is charged to the link Session's allocator and
// reserved before any wire-visible state exists. The link Session outlives
// this owner (EngineNetwork destroys it before the Session).
//
// Wire revisions are link-local: every Publication, FullRepair and baseline
// offer takes the next value of one monotonic link counter, so an atomic
// group is never partially stale on its receiver. World revisions decide
// only WHEN an object is published.
//
// Relevance: an object is visible to this link when its region is 0 (global)
// or one of the link's interest regions, or when the link has no interest
// filter. With a filter, enrollment walks the server's RegionIndex one region
// per step instead of scanning the World. Objects that stop being visible are
// retired. Dormant objects keep their replicas but publish no state until
// woken. Explicit groups (at most 16 objects, 64 KiB) publish atomically
// together, only once every visible member is Ready. Publications are ordered
// by weight times age within an optional per-step byte budget. After a
// publication the active baseline is promoted to the published state; the
// core sender limits that to two promotions per second per object.
// There are no user callbacks anywhere in this class; owner thread only.
#include "private/charged_array.hpp"
#include <superpos/replica_authority.hpp>
#include <superpos/registry.hpp>
#include <superpos/relevance.hpp>
#include <superpos/world.hpp>
#include <array>
#include <optional>

namespace superpos_egp::replication {

// Server-owned per-World-slot replication attributes. Valid only while
// generation equals the live World slot generation (reset on slot reuse).
struct SlotAttributes {
    std::uint64_t indexed{};      // handle currently in the RegionIndex, 0 for none
    std::uint32_t generation{}, region{}, group{};
    std::uint16_t weight{1};
    bool dormant{};
};

struct LinkConfig {
    superpos::ReplicaWireContext context{};
    superpos::PeerId peer{};
    std::uint32_t maximum_active{1000}, maximum_transitions{64};
    // World slots (or indexed region members) visited per step when enrolling.
    std::uint32_t scan_budget{4096};
    // Delta and repair bytes queued per step; 0 means unbounded. At least one
    // batch is always queued so a small budget still makes progress.
    std::uint32_t state_budget_bytes{};
    bool baseline_promotion{true};
    superpos::ReplicaSessionRoutes routes{};
};

struct LinkTotals {
    std::uint64_t steps{}, pumps{}, spawns{}, leaves{}, relevance_leaves{}, ownership_resets{}, publications{}, published_members{},
        group_publications{}, groups_deferred{}, repairs{}, repair_requests{}, promotions{}, deferred{}, budget_limited{},
        capture_pressure{}, sent{}, transport_applied{}, carrier_retired{}, received{}, discarded{}, backpressured{};
};

struct LinkCounts {
    std::uint32_t tracked{}, spawning{}, ready{}, retiring{}, resetting{}, dirty{};
};

// Borrowed for exactly one step. The World must not mutate during the step;
// the server provides it from its owner-thread publish barrier.
struct WorldFrame {
    const superpos::World *world{};
    std::span<const superpos::WorldSlot> slots{};
    std::span<const SlotAttributes> attributes{};
    const superpos::RegionIndex *regions{};
    superpos::Tick tick{};
    std::uint64_t now_milliseconds{}, step{};
};

// Fixed-row types live at namespace scope so ChargedArray can verify they
// are nothrow default constructible (a nested class is incomplete there).
enum class LinkStage : std::uint8_t { Free, NeedBind, NeedOffer, AwaitReady, Ready, NeedLeave, Leaving, NeedReset, NeedResetOffer, AwaitResetPin };
enum class LinkControl : std::uint8_t { None, Bind, Leave, Reset };
struct LinkTracked {
    LinkStage stage{};
    bool repair{}, promotion_pending{}, hidden{};
    std::uint32_t world_slot{}, generation{};
    superpos::ReplicaBinding binding{};
    superpos::PeerId owner{};
    std::uint64_t world_revision{}, ownership_revision{}, dirty_since{}, served_step{};
    // Candidate token of the offer last queued: a pending offer is sent once.
    std::uint64_t queued_offer_token{};
};
struct LinkMapEntry { std::uint32_t world_slot_plus_one{}; std::uint16_t sender_slot{}; };
struct LinkCandidate { std::uint64_t score{}; std::uint16_t sender_slot{}; };

class LinkAuthority {
public:
    static constexpr std::size_t maximum_records = 1064;
    static constexpr std::size_t maximum_interest = 64;
    static constexpr std::size_t maximum_members = superpos::World::maximum_group_objects;
    static constexpr std::size_t compose_bytes = superpos::World::maximum_group_bytes;

    explicit LinkAuthority(superpos::Allocator &) noexcept;
    LinkAuthority(const LinkAuthority &) = delete;
    LinkAuthority &operator=(const LinkAuthority &) = delete;

    // Complete fixed-capacity reservation. No Session access. Only the
    // replica epoch of the context is used; the admitted Session supplies
    // authority/connection epochs and the peer at bind. region_members sizes
    // the enrollment scratch (the server's maximum objects per region).
    superpos::Status reserve(const LinkConfig &, std::size_t state_stride, std::uint64_t incarnation, std::uint32_t region_members) noexcept;
    // Creates the sender store and bridge on an admitted link Session.
    superpos::Status bind(superpos::Session &, superpos::SchemaRegistry &, std::span<const superpos::Schema>,
        superpos::Epoch authority, superpos::Epoch connection, superpos::PeerId peer) noexcept;
    bool reserved() const noexcept { return reserved_; }
    bool bound() const noexcept { return bridge_.has_value(); }
    // Interest regions besides region 0. all=true removes the filter. Regions
    // must be distinct; validation against the server's range is the caller's.
    superpos::Status set_interest(std::span<const std::uint32_t>, bool all) noexcept;
    // Queue lifecycle, state and repair work for this frame. Bounded by the
    // bridge's four immutable captures; refused work is retried next frame.
    superpos::Status step(const WorldFrame &) noexcept;
    // Replaces the plain Session pump for this link once bound.
    superpos::Result<superpos::ReplicaAuthorityProgress> pump(superpos::Tick) noexcept;
    bool raw_allowed(std::uint32_t channel) const noexcept {
        return channel < 32 && channel != config_.routes.control && channel != config_.routes.state && channel != config_.routes.bulk;
    }
    const LinkTotals &totals() const noexcept { return totals_; }
    const LinkConfig &config() const noexcept { return config_; }
    LinkCounts counts() const noexcept;
    superpos::Error failure() const noexcept { return failure_; }
    std::uint64_t incarnation() const noexcept { return incarnation_; }
    std::uint64_t wire_revision() const noexcept { return wire_revision_; }
    bool interest_all() const noexcept { return interest_all_; }
    std::span<const std::uint32_t> interest() const noexcept { return {interest_.data(), interest_count_}; }

private:
    using Stage = LinkStage;
    using Control = LinkControl;
    using Tracked = LinkTracked;
    using MapEntry = LinkMapEntry;
    using Candidate = LinkCandidate;
    struct Batch {
        superpos::ReplicaWireKind kind{};
        std::uint64_t group{};
        std::size_t count{}, delta_bytes{}, state_bytes{};
        std::array<std::uint16_t, maximum_members> members{};
        std::array<std::uint64_t, maximum_members> world_revisions{};
        std::array<std::uint32_t, maximum_members> member_bytes{};
    };

    superpos::Status fail_with(superpos::Error) noexcept;
    const superpos::Schema *schema(superpos::SchemaId) const noexcept;
    superpos::Result<const superpos::SchemaRecord *> catalog(superpos::SchemaId) const noexcept;
    std::size_t probe(std::uint32_t world_slot) const noexcept;
    std::optional<std::uint16_t> find(std::uint32_t world_slot) const noexcept;
    superpos::Status insert(std::uint32_t world_slot, std::uint16_t sender_slot) noexcept;
    void erase(std::uint32_t world_slot) noexcept;
    const SlotAttributes *attributes(const WorldFrame &, std::uint32_t world_slot) const noexcept;
    bool visible(const WorldFrame &, std::uint32_t world_slot) const noexcept;
    superpos::Result<superpos::EntityView> live(const WorldFrame &, const Tracked &) const noexcept;
    superpos::Result<bool> flush_control(const WorldFrame &) noexcept;
    superpos::Result<bool> queue_offer(const WorldFrame &, Tracked &) noexcept;
    // 0: not now (interval, outstanding offer), 1: queued, 2: capture pressure.
    superpos::Result<int> queue_promotion(const WorldFrame &, Tracked &) noexcept;
    superpos::Result<bool> enroll(const WorldFrame &, std::uint32_t world_slot) noexcept;
    superpos::Status enrollment(const WorldFrame &, bool &control_open, bool &capture_open, std::size_t maximum) noexcept;
    // 0: not eligible now, 1: added, 2: batch full or a different group.
    superpos::Result<int> add_member(const WorldFrame &, Batch &, std::uint16_t sender_slot) noexcept;
    superpos::Result<bool> flush_batch(const WorldFrame &, Batch &) noexcept;
    superpos::Status after_send(const WorldFrame &, const Batch &) noexcept;
    superpos::Result<std::uint64_t> next_revision() noexcept;

    ChargedArray<superpos::ReplicaRecord> records_;
    ChargedArray<std::byte> images_, arena_, validation_, freeze_scratch_, compose_;
    ChargedArray<superpos::AuthorityCapture> captures_;
    ChargedArray<superpos::AuthorityExposure> exposures_;
    ChargedArray<superpos::RepairRequest> repairs_;
    ChargedArray<Tracked> tracked_;
    ChargedArray<MapEntry> map_;
    ChargedArray<Candidate> candidates_;
    ChargedArray<superpos::ObjectHandle> region_scratch_;
    ChargedArray<superpos::ReplicaWireMessage> message_;
    std::span<const superpos::Schema> schemas_{};
    std::optional<superpos::FrozenRegistry> registry_;
    std::optional<superpos::PeerReplicas> sender_;
    std::optional<superpos::ReplicaAuthoritySession> bridge_;
    LinkConfig config_{};
    LinkTotals totals_{};
    std::array<std::uint32_t, maximum_interest> interest_{};
    std::size_t interest_count_{}, state_stride_{}, step_bytes_{};
    std::uint64_t incarnation_{}, wire_revision_{};
    std::uint32_t cursor_{}, rotation_{}, region_cursor_{}, region_offset_{};
    Control pending_{};
    std::uint16_t pending_slot_{};
    superpos::Error failure_{};
    bool reserved_{}, interest_all_{true};
};
}
