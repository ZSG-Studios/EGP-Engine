// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/receiver.hpp>
#include <array>
#include <thread>
#include <utility>

namespace superpos_egp::lifecycle {
// Native host identity, never a pointer, script, resource path or wire identity.
// An EGP host must resolve a weak ObjectID before each native operation.
struct Instance { std::uint64_t value{}; bool operator==(const Instance&) const noexcept=default; };
struct Ticket {
    std::uint64_t application{},generation{}; std::uint32_t slot{};
    bool operator==(const Ticket&) const noexcept=default;
};
enum class FactoryCapability : std::uint8_t { None, NativeTransactional };
class Factory {
public:
    virtual ~Factory()=default;
    // Explicit native contract: prepare may fail, and abort reverses even a
    // failing prepare. Neither may access the receiver. Commit cannot allocate,
    // fail, run scripted setters/user callbacks, or destroy the host identity.
    // Host disposal runs separately after take_cleanup(), outside core commit.
    virtual superpos::Status prepare(Instance,superpos::ReplicaChangeKind,const superpos::CanonicalReplica&) noexcept=0;
    virtual void commit(Instance,superpos::ReplicaChangeKind) noexcept=0;
    virtual void abort(Instance,superpos::ReplicaChangeKind) noexcept=0;
};
struct FactoryBinding {
    superpos::SchemaId schema{}; std::uint64_t resource{};
    FactoryCapability capability{}; Factory* factory{};
    // Complete reserved native-instance capacity, including host metadata.
    std::size_t instance_bytes{};
};
enum class ConstructionPhase : std::uint8_t { Empty,Requested,Constructed,Published,Cancelled,Retiring,Exhausted };
struct Record {
    ConstructionPhase phase{}; std::uint64_t generation{1};
    superpos::ReplicaKey key{}; superpos::ObjectHandle handle{};
    Instance instance{}; std::uint16_t factory{}; bool cleanup_taken{};
};
struct Limits {
    std::size_t active{1000},transitions{64};
    // A nonzero seed is useful for qualification near counter exhaustion.
    std::uint64_t initial_generation{1};
    std::size_t native_bytes{64u<<20};
};
struct Construction {
    Ticket ticket{}; superpos::ReplicaKey key{}; superpos::ObjectHandle handle{};
    superpos::SchemaId schema{}; std::uint64_t resource{};
};
struct Cleanup { Ticket ticket{}; Instance instance{}; std::uint64_t resource{}; bool cancel_construction{}; };
struct Counts { std::size_t active{},transitions{},occupied{},exhausted{},native_bytes{}; };

// Fixed caller-owned storage. The receiver alone owns replicated lifecycle and
// canonical state. These rows track native construction and deferred disposal.
class Application final : public superpos::ReplicaApplication {
public:
    class Dispatch {
        friend class Application;
        Application* owner_{};
        explicit Dispatch(Application& owner) noexcept:owner_(&owner){}
    public:
        Dispatch(const Dispatch&)=delete; Dispatch& operator=(const Dispatch&)=delete;
        Dispatch(Dispatch&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)){}
        ~Dispatch();
    };
    Application() noexcept=default;
    superpos::Status initialize(superpos::PeerReplicaReceiver&,
        std::span<const FactoryBinding>,std::span<Record>,Limits={}) noexcept;
    Application(const Application&)=delete; Application& operator=(const Application&)=delete;
    Application(Application&&)=delete;
    Application& operator=(Application&&)=delete;
    // Acquire immediately around receiver operations or a ReplicaReceiverSession
    // pump. Checks the exact receiver instance before it enters its callback.
    superpos::Result<Dispatch> dispatch() noexcept;
    // Called after core bind, before spawn_ready. Resource is a trusted allowlist
    // key, never a wire-controlled path or method. Reserves all row/transition
    // capacity before the host can invoke any native construction factory.
    superpos::Result<Construction> request(superpos::ReplicaKey,std::uint64_t resource) noexcept;
    // Success transfers disposal responsibility to this application. On every
    // failure, the host retains the supplied instance and must dispose it safely.
    superpos::Status complete(Ticket,Instance) noexcept;
    // Invalidate construction/dispatch immediately on session or receiver loss.
    // Cleanup then drains in bounded batches, without consulting that receiver.
    superpos::Status begin_shutdown() noexcept;
    superpos::Result<Cleanup> take_cleanup() noexcept;
    // Cancelled jobs must be settled before acknowledgement. Late complete()
    // always rejects; slot reuse changes the generation or retires the slot.
    superpos::Status acknowledge_cleanup(Ticket) noexcept;
    superpos::Result<Counts> counts() const noexcept;
    superpos::Status stage(superpos::ReplicaChangeKind,std::span<const superpos::CanonicalReplica>) noexcept override;
    void commit() noexcept override;
    void abort() noexcept override;
private:
    superpos::Status available(bool inside_dispatch=false) const noexcept;
    superpos::Result<std::size_t> find(Ticket) const noexcept;
    superpos::Result<std::size_t> find(superpos::ReplicaKey,bool ownership_reset=false) const noexcept;
    Ticket ticket(std::size_t) const noexcept;
    void end_dispatch() noexcept;
    struct Staged { std::size_t index{SIZE_MAX}; superpos::ReplicaKey key{}; bool attempted{},reserved_transition{}; };
    std::thread::id owner_{std::this_thread::get_id()};
    superpos::PeerReplicaReceiver* receiver_{}; std::uint64_t receiver_instance_{},identity_{};
    std::array<FactoryBinding,64> factory_storage_{};
    std::span<const FactoryBinding> factories_{}; std::span<Record> records_{};
    Limits limits_{}; std::size_t active_{},transitions_{},native_bytes_{};
    std::array<Staged,16> staged_{}; std::size_t staged_count_{};
    superpos::ReplicaChangeKind kind_{}; bool dispatching_{},preparing_{},staged_valid_{},stopping_{};
};
}
