// SPDX-License-Identifier: MIT
#pragma once
#include "../lifecycle/lifecycle_application.hpp"
#include <superpos/replica_session.hpp>
#include <optional>

namespace superpos_egp::lifecycle_engine {
// Session-private gate. Raw send/read/applied/retire APIs must call raw_allowed
// before accessing a channel. A second receiver cannot share this gate.
class Routes {
    std::uint32_t mask_{}; std::uint64_t generation_{};
public:
    superpos::Status reserve(superpos::ReplicaSessionRoutes,std::uint64_t) noexcept;
    superpos::Status release(std::uint64_t) noexcept;
    bool raw_read_allowed(std::uint32_t channel) const noexcept { return raw_allowed(channel); }
    bool raw_allowed(std::uint32_t channel) const noexcept { return channel<32 && !(mask_&(std::uint32_t{1}<<channel)); }
};
// Trusted native factories only. begin/poll/cancel/destroy run outside the
// receiver's atomic publication. begin succeeds only after accepting the job;
// every begin error is terminal and followed by cancellation. For poll/cancel,
// Busy/NotReady mean work remains outstanding.
// Successful cancel settles every callback before its job credit is released.
class NativeFactory : public lifecycle::Factory {
public:
    virtual superpos::Status begin(const lifecycle::Construction&) noexcept=0;
    virtual superpos::Result<lifecycle::Instance> poll(const lifecycle::Construction&) noexcept=0;
    virtual superpos::Status cancel(const lifecycle::Construction&) noexcept=0;
    virtual void destroy(lifecycle::Instance) noexcept=0;
};
struct Factory { lifecycle::FactoryBinding binding{}; NativeFactory* native{}; };
struct Job { bool occupied{},complete{}; lifecycle::Construction work{}; };
// Owned in the native Session implementation. Session/receiver/caller pools
// outlive this object. Every pump receives the Session from a current access
// lease; a stored pointer is compared but never dereferenced between leases.
class ReceiverBinding {
public:
    superpos::Status initialize(superpos::Session&,superpos::PeerReplicaReceiver&,Routes&,std::uint64_t generation,
        std::span<const Factory>,std::span<lifecycle::Record>,std::span<Job>,
        std::span<superpos::ReplicaReplySlot>,std::span<superpos::PendingReplicaSpawn>,std::span<std::byte>,
        superpos::ReplicaWireContext,superpos::ReplicaSessionRoutes,lifecycle::Limits={}) noexcept;
    superpos::Result<superpos::ReplicaSessionProgress> pump(superpos::Session&,std::uint64_t generation,superpos::Tick) noexcept;
    // Reentrant close invalidates admission immediately; owner must keep all
    // dependencies alive and pump shutdown until drained() before destruction.
    superpos::Status stop() noexcept;
    bool drained() const noexcept { return drained_; }
    ReceiverBinding() noexcept=default;
    ReceiverBinding(const ReceiverBinding&)=delete;
    ReceiverBinding& operator=(const ReceiverBinding&)=delete;
private:
    superpos::Status cleanup() noexcept;
    superpos::Status shutdown() noexcept;
    NativeFactory* factory(std::uint64_t) noexcept;
    Job* job(lifecycle::Ticket) noexcept;
    superpos::Session* session_{}; superpos::PeerReplicaReceiver* receiver_{};
    Routes* routes_{}; std::uint64_t generation_{}; superpos::SessionIdentity identity_{};
    std::array<Factory,64> factories_{}; std::size_t factory_count_{};
    lifecycle::Application application_;
    std::optional<superpos::ReplicaReceiverSession> bridge_;
    std::span<Job> jobs_{}; std::span<superpos::PendingReplicaSpawn> pending_{};
    std::optional<lifecycle::Cleanup> cleanup_; bool disposed_{};
    std::thread::id owner_{std::this_thread::get_id()};
    bool pumping_{},stopping_{},shutdown_started_{},drained_{},poisoned_{};
};
}
