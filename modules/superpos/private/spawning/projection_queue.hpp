// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/receiver.hpp>
#include <array>
#include <atomic>
#include <span>
#include <thread>

namespace superpos_egp::spawning {
struct Ticket {
    uint32_t slot{};
    uint64_t generation{}, queue{};
    bool operator==(const Ticket &) const noexcept = default;
};
enum class ScenePhase : uint8_t { Pending, Ready, Failed, Retiring };
struct Projection {
    Ticket ticket{};
    superpos::ReplicaKey key{};
    superpos::ObjectHandle handle{};
    uint64_t revision{}, node{};
    uint32_t catalog{};
    bool destroy{};
};
struct Record {
    uint64_t generation{1};
    bool occupied{}, exhausted{}, prepared{}, queued{}, projecting{}, canonical_ready{};
    uint32_t previous{UINT32_MAX}, next{UINT32_MAX}, catalog{};
    superpos::ReplicaKey key{}, staged_key{};
    superpos::ObjectHandle handle{};
    uint64_t revision{}, staged_revision{}, projected_revision{}, node{};
    uint32_t completed_fields{};
    superpos::Error projection_error{};
    ScenePhase scene{ScenePhase::Pending};
    bool staged_destroy{};
    // Spawn readiness: monotonic microseconds at construction reservation. A
    // never-published row past its deadline is held as Failed/Timeout after a
    // late initial state, until an explicit retry clears the hold.
    bool timed_out{};
    uint64_t pending_since{};
};

// Exactly one intrusive job cell per admitted proxy. Preparation/publication
// only changes native metadata. Scene operations run after take(), and must
// revalidate the returned ticket after every callback before touching a row.
// No queue allocation, setter, signal, Ref manipulation or ObjectDB lookup can
// occur inside commit(). Caller-owned rows outlive this owner-thread queue.
class ProjectionQueue {
    std::span<Record> rows_;
    std::thread::id owner_{std::this_thread::get_id()};
    uint32_t head_{UINT32_MAX}, tail_{UINT32_MAX};
    bool initialized_{}, dispatching_{}, poisoned_{};
    Ticket active_{};
    static uint64_t new_identity() noexcept {
        static std::atomic<uint64_t> next{1};
        auto value = next.load(std::memory_order_relaxed);
        for (;;) {
            if (value == UINT64_MAX) return 0;
            if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
        }
    }
    const uint64_t identity_{new_identity()};
    bool owner() const noexcept { return std::this_thread::get_id() == owner_; }
    Record *find(Ticket ticket) noexcept {
        if (!initialized_ || !owner() || ticket.queue != identity_ || ticket.slot >= rows_.size()) return nullptr;
        auto &row = rows_[ticket.slot];
        return row.occupied && row.generation == ticket.generation ? &row : nullptr;
    }
    void unlink(uint32_t slot) noexcept {
        auto &row = rows_[slot];
        if (!row.queued) return;
        if (row.previous == UINT32_MAX) head_ = row.next; else rows_[row.previous].next = row.next;
        if (row.next == UINT32_MAX) tail_ = row.previous; else rows_[row.next].previous = row.previous;
        row.previous = row.next = UINT32_MAX; row.queued = false;
    }
    void enqueue(uint32_t slot) noexcept {
        auto &row = rows_[slot];
        if (row.queued) return;
        row.previous = tail_; row.next = UINT32_MAX; row.queued = true;
        if (tail_ == UINT32_MAX) head_ = slot; else rows_[tail_].next = slot;
        tail_ = slot;
    }
public:
    ProjectionQueue() noexcept = default;
    ProjectionQueue(const ProjectionQueue &) = delete;
    ProjectionQueue &operator=(const ProjectionQueue &) = delete;
    superpos::Status initialize(std::span<Record> rows, uint64_t generation = 1) noexcept {
        if (!owner()) return superpos::fail(superpos::Error::Busy);
        if (!identity_) return superpos::fail(superpos::Error::CounterExhausted);
        if (initialized_ || rows.empty() || rows.size() > 1064 || !generation) return superpos::fail(superpos::Error::InvalidArgument);
        for (auto &row : rows) { row = Record{}; row.generation = generation; }
        rows_ = rows; initialized_ = true; return {};
    }
    superpos::Result<Ticket> reserve(uint32_t catalog, superpos::ObjectHandle handle, uint64_t now = 0) noexcept {
        if (!owner()) return superpos::fail(superpos::Error::Busy);
        if (poisoned_) return superpos::fail(superpos::Error::ProtocolViolation);
        if (!initialized_ || !handle) return superpos::fail(superpos::Error::InvalidArgument);
        for (uint32_t slot = 0; slot < rows_.size(); ++slot) {
            auto &row = rows_[slot];
            if (row.occupied || row.exhausted) continue;
            row.occupied = true; row.catalog = catalog; row.handle = handle; row.pending_since = now;
            return Ticket{slot, row.generation, identity_};
        }
        return superpos::fail(superpos::Error::CapacityExceeded);
    }
    superpos::Status prepare(Ticket ticket, superpos::ReplicaChangeKind kind,
            const superpos::CanonicalReplica &state) noexcept {
        auto *row = find(ticket);
        if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        if (row->prepared) return superpos::fail(superpos::Error::Busy);
        // A bound replica may be retired before its first canonical image.
        // Only that never-published Pending row has a valid revision-zero
        // Destroy; zero cannot reset a published or already retired row.
        const bool pending_destroy = kind == superpos::ReplicaChangeKind::Destroy && !state.revision &&
            !row->revision && !row->canonical_ready && row->scene == ScenePhase::Pending;
        if (state.handle != row->handle || (!state.revision && !pending_destroy) || !state.key.slot || !state.key.incarnation ||
                !state.key.authority_epoch || !state.key.connection_epoch || !state.key.replica_epoch || !state.key.encoding_epoch)
            return superpos::fail(superpos::Error::InvalidArgument);
        row->staged_key = state.key; row->staged_revision = state.revision;
        row->staged_destroy = kind == superpos::ReplicaChangeKind::Destroy;
        row->prepared = true; return {};
    }
    void abort(Ticket ticket) noexcept { if (auto *row = find(ticket)) row->prepared = false; }
    void commit(Ticket ticket) noexcept {
        auto *row = find(ticket);
        if (!row || !row->prepared) { poisoned_ = true; return; }
        row->prepared = false; row->key = row->staged_key; row->revision = row->staged_revision;
        row->canonical_ready = !row->staged_destroy;
        row->completed_fields = 0; row->projection_error = superpos::Error{};
        if (row->timed_out && !row->staged_destroy) {
            // Canonical state stays consistent with the sender; only scene
            // projection is withheld. No queue cell is consumed.
            row->scene = ScenePhase::Failed; row->projection_error = superpos::Error::Timeout;
            return;
        }
        row->scene = row->staged_destroy ? ScenePhase::Retiring : ScenePhase::Pending;
        enqueue(ticket.slot);
    }
    // Bounded by the fixed rows. Marks never-published, still-pending rows whose
    // readiness deadline has passed. Returns the number newly timed out.
    uint32_t expire(uint64_t now, uint64_t timeout) noexcept {
        if (!owner() || !initialized_ || poisoned_ || !timeout) return 0;
        uint32_t expired = 0;
        for (auto &row : rows_) {
            if (!row.occupied || row.timed_out || row.prepared || row.canonical_ready || row.revision ||
                    row.scene != ScenePhase::Pending || now < row.pending_since || now - row.pending_since < timeout) continue;
            row.timed_out = true; ++expired;
        }
        return expired;
    }
    superpos::Status retry(Ticket ticket) noexcept {
        auto *row = find(ticket);
        if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        if (poisoned_ || row->projecting || !row->canonical_ready || row->scene != ScenePhase::Failed)
            return superpos::fail(superpos::Error::Busy);
        row->scene = ScenePhase::Pending; row->completed_fields = 0; row->projection_error = {}; row->timed_out = false;
        enqueue(ticket.slot); return {};
    }
    superpos::Result<Projection> take() noexcept {
        if (!owner() || dispatching_) return superpos::fail(superpos::Error::Busy);
        if (poisoned_) return superpos::fail(superpos::Error::ProtocolViolation);
        if (!initialized_ || head_ == UINT32_MAX) return superpos::fail(superpos::Error::NotReady);
        const uint32_t slot = head_; auto &row = rows_[slot];
        unlink(slot); row.projecting = true; dispatching_ = true; active_ = {slot, row.generation, identity_};
        return Projection{{slot, row.generation, identity_}, row.key, row.handle, row.revision, row.node, row.catalog,
            row.scene == ScenePhase::Retiring};
    }
    superpos::Status set_node(Ticket ticket, uint64_t node) noexcept {
        auto *row = find(ticket); if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        row->node = node; return {};
    }
    superpos::Status complete(const Projection &job, uint32_t completed_fields,
            superpos::Error error = {}) noexcept {
        if (!owner() || !dispatching_ || active_ != job.ticket) return superpos::fail(superpos::Error::InvalidArgument);
        dispatching_ = false;
        auto *row = find(job.ticket);
        if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        row->projecting = false;
        if (row->key != job.key || row->revision != job.revision) return superpos::fail(superpos::Error::StaleGeneration);
        row->completed_fields = completed_fields;
        row->projection_error = error; row->projected_revision = job.revision;
        row->scene = job.destroy ? ScenePhase::Retiring : error == superpos::Error{} ? ScenePhase::Ready : ScenePhase::Failed;
        return {};
    }
    superpos::Result<Record> inspect(Ticket ticket) noexcept {
        auto *row = find(ticket); if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        return *row;
    }
    // Invalidate before deleting the returned weak node. Reentrant callbacks
    // cannot publish to the retired ticket, even when this slot is reused.
    superpos::Result<uint64_t> release(Ticket ticket) noexcept {
        auto *row = find(ticket); if (!row) return superpos::fail(superpos::Error::StaleGeneration);
        const uint64_t node = row->node, generation = row->generation;
        unlink(ticket.slot); *row = Record{};
        if (generation == UINT64_MAX) { row->generation = generation; row->exhausted = true; }
        else row->generation = generation + 1;
        return node;
    }
};
}
