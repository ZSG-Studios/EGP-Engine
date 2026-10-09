// SPDX-License-Identifier: MIT
#pragma once
#include "superpos_spawn_catalog.h"
#include "superpos_replica_view.h"
#include "projection_queue.hpp"
#include "private/lifecycle_engine/staged/native_receiver.hpp"
#include "private/charged_array.hpp"

class SuperposSession;
class SuperposSpawner;

namespace superpos_egp::spawning {
using lifecycle_engine::SuperposNativeFactory;
class SuperposSpawnRuntime;
class SuperposSpawnProxy final : public RefCounted {
    GDCLASS(SuperposSpawnProxy, RefCounted);
    friend class SuperposSpawnRuntime;
    friend class SuperposSpawnFactory;
    ObjectID runtime;
    Ticket ticket{};
protected:
    static void _bind_methods() {}
};

class SuperposSpawnFactory final : public SuperposNativeFactory {
    GDCLASS(SuperposSpawnFactory, SuperposNativeFactory);
    Ref<SuperposSpawnRuntime> runtime_;
    uint32_t catalog_{};
protected:
    static void _bind_methods() {}
    bool accepts_native(const Object &) const noexcept override;
    superpos::Status prepare_native(Object &, superpos::ReplicaChangeKind, const superpos::CanonicalReplica &) noexcept override;
    void commit_native(Object &, superpos::ReplicaChangeKind) noexcept override;
    void abort_native(Object &, superpos::ReplicaChangeKind) noexcept override;
    void destroy_native(Object &) noexcept override;
public:
    void initialize(const Ref<SuperposSpawnRuntime> &, uint32_t);
    superpos::Status begin(const lifecycle::Construction &) noexcept override;
    superpos::Result<lifecycle::Instance> poll(const lifecycle::Construction &) noexcept override;
    superpos::Status cancel(const lifecycle::Construction &) noexcept override;
    superpos::Status quiesce_native() noexcept override;
};

// Independently owned quota: outstanding weak-view/projection operations never
// retain a reference to a destroyed Session allocator. The module backing
// allocator is process-owned. All fixed rows/proxies precede receiver admission.
class SuperposSpawnRuntime final : public RefCounted {
    GDCLASS(SuperposSpawnRuntime, RefCounted);
    friend class SuperposSpawnFactory;
    struct Slot { lifecycle::Ticket construction; Ticket projection; bool used; Slot() noexcept : construction{}, projection{}, used(false) {} };
    superpos::QuotaAllocator quota_;
    ChargedArray<Record> rows_{quota_, superpos::MemoryDomain::Replication};
    ChargedArray<Slot> slots_{quota_, superpos::MemoryDomain::Replication};
    std::array<Ref<SuperposSpawnProxy>, 1064> proxies_;
    ProjectionQueue queue_;
    SuperposSpawnCatalog::Snapshot catalog_;
    ObjectID spawner_, session_;
    // Root of the single owner-thread projection on the stack. Retirement that
    // reenters from its setters, constructor or tree callbacks invalidates the
    // ticket immediately but defers deleting that root until the frame unwinds.
    ObjectID projecting_root_, deferred_delete_;
    uint64_t binding_{}, session_binding_{}, spawn_timeout_us_{15000000};
    bool initialized_{}, stopping_{}, projecting_{};
    void finish_deferred_delete() noexcept;
    Slot *find(lifecycle::Ticket) noexcept;
    void release(Ticket) noexcept;
    bool live(const Projection &) noexcept;
    Error project_one(const Projection &);
protected:
    static void _bind_methods() {}
public:
    static Ref<SuperposSpawnRuntime> create();
    Error initialize(superpos::BudgetAllocator &, SuperposSpawnCatalog::Snapshot,
        SuperposSpawner &, SuperposSession &, uint64_t, uint64_t, uint32_t, size_t, uint64_t spawn_timeout_ms = 15000);
    superpos::Status registrations(std::span<lifecycle_engine::Registration>) noexcept;
    Dictionary project(uint32_t);
    Dictionary status() const;
    Dictionary projection_status(uint64_t, const superpos::ReplicaKey &) const;
    Error retry(uint64_t, const superpos::ReplicaKey &);
    TypedArray<SuperposReplicaView> views(uint32_t, uint32_t);
    void abandon_spawner() noexcept;
    void request_stop() noexcept { stopping_ = true; }
    superpos::Status quiesce() noexcept;
};
}
