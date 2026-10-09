// SPDX-License-Identifier: MIT
// Original experimental completion storage. No RTC consumer is converted by this file.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace superpos::certificate_completion {
enum class Error : std::uint8_t {
    NotReady, Full, ByteLimit, Contended, Closed, Unsupported, Exhausted,
    Cancelled, OutOfMemory, ProviderFailure, WrongThread
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
template<class T> using Result = std::expected<T, Error>;
inline std::uint64_t next_instance() noexcept {
    static std::atomic<std::uint64_t> next{0};
    auto value = next.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint64_t>::max()) {
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
    }
    return 0;
}

// T must have a nonallocating copy/move and no-throw destructor. A shared_ptr
// certificate is the intended integration type; noexcept traits alone do not
// prove a custom type is nonallocating. Configured types need separate auditing.
// This is not a claim about its pointee's
// crypto/provider allocation budget. Retained payload charge is supplied explicitly.
template<class T, std::size_t Capacity = 64, std::size_t ByteLimit = 512 * 1024,
         std::uint64_t GenerationLimit = std::numeric_limits<std::uint64_t>::max(),
         std::uint32_t RefLimit = std::numeric_limits<std::uint32_t>::max()>
class Pool {
    static_assert(Capacity > 0 && GenerationLimit > 0 && RefLimit >= 2);
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_copy_constructible_v<T> &&
                  std::is_nothrow_destructible_v<T>);
    enum class Phase : std::uint8_t { Free, Pending, Publishing, Ready, Failed, Reclaiming, Reclaimed };
    static_assert(std::atomic<Phase>::is_always_lock_free);
    struct Cell {
        std::atomic<Phase> phase{Phase::Free};
        std::atomic<std::uint32_t> references{0};
        std::uint64_t generation{};
        std::size_t charge{};
        std::optional<T> value;
        Error failure{Error::NotReady};
    };
    std::array<Cell, Capacity> cells_{};
    const std::uint64_t instance_{next_instance()};
    const std::thread::id reclaimer_{std::this_thread::get_id()};
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> accepting_{true};
    std::size_t occupied_{}, bytes_{};
    struct Lock {
        Pool* pool{};
        explicit Lock(Pool* p) noexcept {
            if (!p->gate_.test_and_set(std::memory_order_acquire)) pool = p;
        }
        ~Lock() { if (pool) pool->gate_.clear(std::memory_order_release); }
        explicit operator bool() const noexcept { return pool != nullptr; }
    };
    static bool terminal(Phase phase) noexcept { return phase == Phase::Ready || phase == Phase::Failed; }
    bool valid(std::size_t slot, std::uint64_t generation, std::uint64_t instance) const noexcept {
        return instance == instance_ && instance != 0 && slot < Capacity && cells_[slot].generation == generation;
    }
    void release(std::size_t slot) noexcept {
        // Holding a token pins the cell, so reuse cannot race this decrement.
        auto old = cells_[slot].references.fetch_sub(1, std::memory_order_acq_rel);
        if (!old) std::terminate();
    }
public:
    using value_type = T;
    class Producer;
    class Ticket {
        friend class Pool;
        Pool* pool_{};
        std::size_t slot_{};
        std::uint64_t generation_{}, instance_{};
        Ticket(Pool* p, std::size_t slot, std::uint64_t generation) noexcept
            : pool_(p), slot_(slot), generation_(generation), instance_(p->instance_) {}
    public:
        Ticket() noexcept = default;
        Ticket(const Ticket&) = delete;
        Ticket& operator=(const Ticket&) = delete;
        Ticket(Ticket&& other) noexcept { swap(other); }
        Ticket& operator=(Ticket&& other) noexcept { if (this != &other) { reset(); swap(other); } return *this; }
        ~Ticket() { reset(); }
        explicit operator bool() const noexcept { return pool_ != nullptr; }
        // No implicit refcounting copy with an unreportable overflow boundary.
        Result<Ticket> clone() const noexcept {
            if (!pool_ || !pool_->valid(slot_, generation_, instance_)) return std::unexpected(Error::Closed);
            auto& references = pool_->cells_[slot_].references;
            auto count = references.load(std::memory_order_acquire);
            if (!count || count >= RefLimit) return std::unexpected(count ? Error::Exhausted : Error::Closed);
            if (references.compare_exchange_strong(count, count + 1, std::memory_order_acq_rel))
                return Ticket(pool_, slot_, generation_);
            return std::unexpected(Error::Contended);
        }
        Result<T> poll() const noexcept {
            if (!pool_ || !pool_->valid(slot_, generation_, instance_)) return std::unexpected(Error::Closed);
            auto& cell = pool_->cells_[slot_];
            auto phase = cell.phase.load(std::memory_order_acquire);
            if (phase == Phase::Ready) return std::as_const(*cell.value);
            if (phase == Phase::Failed) return std::unexpected(cell.failure);
            return std::unexpected(Error::NotReady);
        }
        void reset() noexcept {
            if (!pool_) return;
            auto* p = pool_; auto slot = slot_; pool_ = nullptr;
            p->release(slot); // no T destruction, callback or blocking lock here
        }
    private:
        void swap(Ticket& other) noexcept {
            std::swap(pool_, other.pool_); std::swap(slot_, other.slot_);
            std::swap(generation_, other.generation_); std::swap(instance_, other.instance_);
        }
    };
    class Producer {
        friend class Pool;
        Pool* pool_{};
        std::size_t slot_{};
        std::uint64_t generation_{}, instance_{};
        Producer(Pool* p, std::size_t slot, std::uint64_t generation) noexcept
            : pool_(p), slot_(slot), generation_(generation), instance_(p->instance_) {}
    public:
        Producer() noexcept = default;
        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;
        Producer(Producer&& other) noexcept { swap(other); }
        Producer& operator=(Producer&& other) noexcept { if (this != &other) { cancel(); swap(other); } return *this; }
        ~Producer() { cancel(); }
        explicit operator bool() const noexcept { return pool_ != nullptr; }
        // Rejection preserves the caller's value. This producer is single-owner.
        Result<void> complete(T& value) noexcept {
            if (!pool_ || !pool_->valid(slot_, generation_, instance_)) return std::unexpected(Error::Closed);
            auto& cell = pool_->cells_[slot_];
            if (cell.phase.load(std::memory_order_acquire) != Phase::Pending) return std::unexpected(Error::Closed);
            auto* p = pool_; auto slot = slot_; pool_ = nullptr;
            cell.phase.store(Phase::Publishing, std::memory_order_release);
            // The local producer reference pins storage across a custom move.
            // Reentrant cancellation sees the consumed producer handle.
            cell.value.emplace(std::move(value));
            cell.phase.store(Phase::Ready, std::memory_order_release);
            p->release(slot); return {};
        }
        Result<void> fail(Error error) noexcept {
            if (error != Error::Cancelled && error != Error::OutOfMemory && error != Error::ProviderFailure && error != Error::Closed)
                return std::unexpected(Error::Unsupported);
            if (!pool_ || !pool_->valid(slot_, generation_, instance_)) return std::unexpected(Error::Closed);
            auto& cell = pool_->cells_[slot_];
            if (cell.phase.load(std::memory_order_acquire) != Phase::Pending) return std::unexpected(Error::Closed);
            cell.failure = error;
            cell.phase.store(Phase::Failed, std::memory_order_release);
            release(); return {};
        }
        void cancel() noexcept { if (pool_) (void)fail(Error::Cancelled); }
    private:
        void release() noexcept { auto* p = pool_; auto slot = slot_; pool_ = nullptr; p->release(slot); }
        void swap(Producer& other) noexcept {
            std::swap(pool_, other.pool_); std::swap(slot_, other.slot_);
            std::swap(generation_, other.generation_); std::swap(instance_, other.instance_);
        }
    };
    struct Reservation { Producer producer; Ticket ticket; };
    struct Snapshot { std::size_t occupied{}, retained_payload_bytes{}, fixed_storage_bytes{}; bool accepting{}; };
    struct Drain { Error status{Error::NotReady}; std::size_t reclaimed{}; bool complete{}; };
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() {
        // Borrowed pool must outlive all producers/tickets. Never destroy a
        // result on a foreign thread or detach a still-borrowing task to exit.
        if (std::this_thread::get_id() != reclaimer_) std::terminate();
        close(); auto result = drain();
        if (!result.complete || occupied_) std::terminate();
    }
    Result<Reservation> try_reserve(std::size_t retained_payload_bytes) noexcept {
        Lock lock(this); if (!lock) return std::unexpected(Error::Contended);
        if (!instance_) return std::unexpected(Error::Exhausted);
        if (!accepting_.load(std::memory_order_acquire)) return std::unexpected(Error::Closed);
        if (retained_payload_bytes > ByteLimit - bytes_) return std::unexpected(Error::ByteLimit);
        bool exhausted = false;
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& cell = cells_[slot];
            if (cell.phase.load(std::memory_order_acquire) != Phase::Free) continue;
            if (cell.generation == GenerationLimit) { exhausted = true; continue; }
            ++cell.generation; cell.charge = retained_payload_bytes;
            cell.references.store(2, std::memory_order_release);
            cell.phase.store(Phase::Pending, std::memory_order_release);
            ++occupied_; bytes_ += retained_payload_bytes;
            return Reservation{Producer(this, slot, cell.generation), Ticket(this, slot, cell.generation)};
        }
        return std::unexpected(exhausted ? Error::Exhausted : Error::Full);
    }
    void close() noexcept { accepting_.store(false, std::memory_order_release); }
    Result<Snapshot> snapshot() noexcept {
        Lock lock(this); if (!lock) return std::unexpected(Error::Contended);
        return Snapshot{occupied_, bytes_, sizeof(cells_), accepting_.load(std::memory_order_acquire)};
    }
    Drain drain() noexcept {
        if (std::this_thread::get_id() != reclaimer_) return {Error::WrongThread, 0, false};
        Drain result;
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& cell = cells_[slot]; bool destroy = false;
            {
                Lock lock(this); if (!lock) { result.status = Error::Contended; return result; }
                const auto phase = cell.phase.load(std::memory_order_acquire);
                if (terminal(phase) && cell.references.load(std::memory_order_acquire) == 0) {
                    cell.phase.store(Phase::Reclaiming, std::memory_order_release); destroy = true;
                }
            }
            if (destroy) {
                cell.value.reset(); // final owner/provider destruction outside admission lock
                cell.phase.store(Phase::Reclaimed, std::memory_order_release);
            }
            {
                Lock lock(this); if (!lock) { result.status = Error::Contended; return result; }
                if (cell.phase.load(std::memory_order_acquire) == Phase::Reclaimed) {
                    bytes_ -= cell.charge; --occupied_; ++result.reclaimed;
                    cell.phase.store(Phase::Free, std::memory_order_release);
                }
            }
        }
        Lock lock(this); if (!lock) { result.status = Error::Contended; return result; }
        result.complete = occupied_ == 0; return result;
    }
};
} // namespace superpos::certificate_completion
