// SPDX-License-Identifier: MIT
#include "spawn_runtime.hpp"
#include "receiver_public_access.hpp"
#include "superpos_spawner.h"
#include "core/os/thread.h"
#include "core/object/class_db.h"
#include <cstring>

namespace superpos_egp::spawning {
namespace {
Error engine_error(superpos::Error error) {
    switch (error) {
        case superpos::Error::None: return OK;
        case superpos::Error::Busy: return ERR_BUSY;
        case superpos::Error::OutOfMemory: return ERR_OUT_OF_MEMORY;
        case superpos::Error::CapacityExceeded: return ERR_OUT_OF_MEMORY;
        case superpos::Error::NotReady: return ERR_UNCONFIGURED;
        default: return ERR_INVALID_DATA;
    }
}

template <class T> Ref<T> new_native() {
    void *memory = Memory::alloc_static(sizeof(T));
    if (!memory) return {};
    return Ref<T>(memnew_placement(memory, T));
}
std::array<uint64_t, 6> identity(const superpos::ReplicaKey &key) {
    return {key.slot, key.incarnation, key.authority_epoch, key.connection_epoch, key.replica_epoch, key.encoding_epoch};
}
}

Ref<SuperposSpawnRuntime> SuperposSpawnRuntime::create() { return new_native<SuperposSpawnRuntime>(); }

Error SuperposSpawnRuntime::initialize(superpos::BudgetAllocator &backing,
        SuperposSpawnCatalog::Snapshot catalog, SuperposSpawner &spawner,
        SuperposSession &session, uint64_t binding, uint64_t session_binding, uint32_t capacity, size_t native_limit) {
    if (!Thread::is_main_thread() || initialized_ || !binding || !session_binding || !capacity || capacity > 1064 || !native_limit || native_limit > (64u << 20)) return ERR_INVALID_PARAMETER;
    superpos::MemoryPlan plan; plan.limits.fill(0);
    plan.limits[unsigned(superpos::MemoryDomain::Replication)] = native_limit;
    auto bound = quota_.bind(backing, plan); if (!bound) return engine_error(bound.error());
    auto rows = rows_.initialize(capacity); if (!rows) return engine_error(rows.error());
    auto slots = slots_.initialize(capacity); if (!slots) return engine_error(slots.error());
    auto queue = queue_.initialize(rows_.span()); if (!queue) return engine_error(queue.error());
    for (uint32_t i = 0; i < capacity; ++i) {
        proxies_[i] = new_native<SuperposSpawnProxy>();
        if (proxies_[i].is_null()) return ERR_OUT_OF_MEMORY;
        proxies_[i]->runtime = get_instance_id();
    }
    catalog_ = std::move(catalog); spawner_ = spawner.get_instance_id(); session_ = session.get_instance_id(); binding_ = binding; session_binding_ = session_binding;
    initialized_ = true; return OK;
}

superpos::Status SuperposSpawnRuntime::registrations(std::span<lifecycle_engine::Registration> output) noexcept {
    if (!initialized_ || output.size() != catalog_.entry_count) return superpos::fail(superpos::Error::InvalidArgument);
    Ref<SuperposSpawnRuntime> pin(this);
    for (uint32_t i = 0; i < catalog_.entry_count; ++i) {
        Ref<SuperposSpawnFactory> factory = new_native<SuperposSpawnFactory>();
        if (factory.is_null()) return superpos::fail(superpos::Error::OutOfMemory);
        factory->initialize(pin, i);
        output[i] = {catalog_.entries[i].schema, catalog_.entries[i].resource,
            sizeof(SuperposSpawnProxy) + sizeof(Record) + sizeof(Slot), factory};
    }
    return {};
}

SuperposSpawnRuntime::Slot *SuperposSpawnRuntime::find(lifecycle::Ticket ticket) noexcept {
    for (auto &slot : slots_.span()) if (slot.used && slot.construction == ticket) return &slot;
    return nullptr;
}
void SuperposSpawnRuntime::release(Ticket ticket) noexcept {
    if (ticket.slot >= slots_.size()) return;
    auto &slot = slots_[ticket.slot];
    if (!slot.used || slot.projection != ticket) return;
    auto released = queue_.release(ticket);
    if (!released) return;
    slot = {}; proxies_[ticket.slot]->ticket = {};
    // Invalidation precedes user destructors. Reentrant close/rebind observes a
    // retired ticket and cannot dispose this node twice or revive the proxy.
    const ObjectID node_id(*released);
    if (projecting_ && node_id.is_valid() && node_id == projecting_root_) {
        // The root's setter, constructor or tree callback is still executing.
        // Deleting it here would free the frame's object; delete on unwind.
        deferred_delete_ = node_id;
        return;
    }
    if (auto *node = Object::cast_to<Node>(ObjectDB::get_instance(node_id))) memdelete(node);
}
void SuperposSpawnRuntime::finish_deferred_delete() noexcept {
    const ObjectID node_id = deferred_delete_;
    deferred_delete_ = ObjectID{};
    if (auto *node = Object::cast_to<Node>(ObjectDB::get_instance(node_id))) memdelete(node);
}

void SuperposSpawnFactory::initialize(const Ref<SuperposSpawnRuntime> &runtime, uint32_t catalog) { runtime_ = runtime; catalog_ = catalog; }
superpos::Status SuperposSpawnFactory::begin(const lifecycle::Construction &work) noexcept {
    // Every begin error is terminal for the receiver. A pending stop therefore
    // accepts the bounded job and poll() withholds its proxy until the stop
    // completes and cancels it, or a rejected stop request is retried.
    if (runtime_.is_null() || !ObjectDB::get_instance(runtime_->spawner_) || catalog_ >= runtime_->catalog_.entry_count) return superpos::fail(superpos::Error::NotReady);
    const auto &entry = runtime_->catalog_.entries[catalog_];
    if (entry.resource != work.resource || entry.schema != work.schema || runtime_->find(work.ticket)) return superpos::fail(superpos::Error::InvalidArgument);
    auto reserved = runtime_->queue_.reserve(catalog_, work.handle); if (!reserved) return superpos::fail(reserved.error());
    auto &slot = runtime_->slots_[reserved->slot]; slot.used = true; slot.construction = work.ticket; slot.projection = *reserved;
    runtime_->proxies_[reserved->slot]->ticket = *reserved;
    return {};
}
superpos::Result<lifecycle::Instance> SuperposSpawnFactory::poll(const lifecycle::Construction &work) noexcept {
    auto *slot = runtime_.is_valid() ? runtime_->find(work.ticket) : nullptr;
    if (!slot || runtime_->stopping_) return superpos::fail(superpos::Error::NotReady);
    return lifecycle::Instance{uint64_t(runtime_->proxies_[slot->projection.slot]->get_instance_id())};
}
superpos::Status SuperposSpawnFactory::cancel(const lifecycle::Construction &work) noexcept {
    if (runtime_.is_valid()) if (auto *slot = runtime_->find(work.ticket)) runtime_->release(slot->projection);
    return {};
}
bool SuperposSpawnFactory::accepts_native(const Object &object) const noexcept {
    auto *proxy = Object::cast_to<SuperposSpawnProxy>(&object);
    if (!proxy || runtime_.is_null() || proxy->runtime != runtime_->get_instance_id() || proxy->ticket.slot >= runtime_->slots_.size()) return false;
    const auto &slot = runtime_->slots_[proxy->ticket.slot];
    return slot.used && slot.projection == proxy->ticket;
}
superpos::Status SuperposSpawnFactory::prepare_native(Object &object, superpos::ReplicaChangeKind kind, const superpos::CanonicalReplica &state) noexcept {
    // Canonical metadata keeps tracking the receiver during a pending stop;
    // only scene projection halts. Failing here would fail the whole receiver.
    if (!ObjectDB::get_instance(runtime_->spawner_)) return superpos::fail(superpos::Error::NotReady);
    const auto &entry = runtime_->catalog_.entries[catalog_];
    if (!state.schema || state.schema->id() != entry.schema) return superpos::fail(superpos::Error::IncompatibleSchema);
    for (uint32_t i = 0; i < entry.field_count; ++i) if (!state.schema->field(entry.fields[i].id)) return superpos::fail(superpos::Error::IncompatibleSchema);
    return runtime_->queue_.prepare(Object::cast_to<SuperposSpawnProxy>(&object)->ticket, kind, state);
}
void SuperposSpawnFactory::commit_native(Object &object, superpos::ReplicaChangeKind) noexcept { runtime_->queue_.commit(Object::cast_to<SuperposSpawnProxy>(&object)->ticket); }
void SuperposSpawnFactory::abort_native(Object &object, superpos::ReplicaChangeKind) noexcept { runtime_->queue_.abort(Object::cast_to<SuperposSpawnProxy>(&object)->ticket); }
void SuperposSpawnFactory::destroy_native(Object &object) noexcept { runtime_->release(Object::cast_to<SuperposSpawnProxy>(&object)->ticket); }
superpos::Status SuperposSpawnFactory::quiesce_native() noexcept { return runtime_.is_valid() ? runtime_->quiesce() : superpos::Status{}; }

bool SuperposSpawnRuntime::live(const Projection &job) noexcept {
    if (stopping_ || !ObjectDB::get_instance(spawner_)) return false;
    const auto row = queue_.inspect(job.ticket);
    if (!row || row->key != job.key || row->revision != job.revision) return false;
    Ref<SuperposSession> session(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_)));
    uint64_t binding = 0;
    if (session.is_null() || session->query_binding_generation(binding) != OK || binding != session_binding_) return false;
    const auto after_pin = queue_.inspect(job.ticket);
    return !stopping_ && after_pin && after_pin->key == job.key && after_pin->revision == job.revision;
}

Error SuperposSpawnRuntime::project_one(const Projection &job) {
    if (!live(job) || job.catalog >= catalog_.entry_count) return ERR_UNCONFIGURED;
    const auto &entry = catalog_.entries[job.catalog];
    PackedInt64Array fields;
    Error resized = fields.resize(entry.field_count); if (resized != OK) return resized;
    for (uint32_t i = 0; i < entry.field_count; ++i) { int64_t bits; std::memcpy(&bits, &entry.fields[i].id, sizeof(bits)); fields.set(i, bits); }
    Ref<SuperposSession> session(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_)));
    if (session.is_null()) return ERR_UNCONFIGURED;
    const Dictionary image = SuperposReceiverPublicAccess::read(*session.ptr(), binding_, job.handle.value, identity(job.key), &fields);
    if (int(image.get("error", ERR_UNCONFIGURED)) != OK) return Error(int(image["error"]));
    if (uint64_t(int64_t(image["revision"])) != job.revision || !live(job)) return ERR_BUSY;
    const Dictionary values = image["values"];
    ObjectID root_id(job.node);
    bool constructed = false;
    if (!root_id.is_valid()) {
        Node *root = entry.scene->instantiate();
        if (!root) return ERR_CANT_CREATE;
        root_id = root->get_instance_id();
        if (!live(job) || !queue_.set_node(job.ticket, uint64_t(root_id))) { memdelete(root); return ERR_BUSY; }
        projecting_root_ = root_id;
        constructed = true;
    } else {
        Node *root = Object::cast_to<Node>(ObjectDB::get_instance(root_id));
        if (!root) return ERR_DOES_NOT_EXIST;
        projecting_root_ = root_id;
        constructed = root->get_parent() == nullptr;
        if (!constructed && root->get_parent()->get_instance_id() != spawner_) return ERR_BUSY;
    }
    uint32_t completed = 0;
    // Preserve the exact applied prefix on every interruption after projection
    // starts. complete() revalidates the ticket/revision, so reentrant retirement
    // cannot publish this result onto a replacement object.
    struct ProjectionCompletion {
        ProjectionQueue &queue;
        const Projection &job;
        uint32_t &completed;
        bool finished = false;
        ~ProjectionCompletion() { if (!finished) (void)queue.complete(job, completed, superpos::Error::InvalidArgument); }
    } completion{queue_, job, completed};
    while (completed < entry.field_count) {
        if (!live(job)) return ERR_BUSY;
        Node *root = Object::cast_to<Node>(ObjectDB::get_instance(root_id));
        if (!root) return ERR_DOES_NOT_EXIST;
        const auto &field = entry.fields[completed];
        Node *target = field.node.is_empty() ? root : root->get_node_or_null(field.node);
        if (!target) return ERR_DOES_NOT_EXIST;
        const ObjectID target_id = target->get_instance_id();
        int64_t field_bits; std::memcpy(&field_bits, &field.id, sizeof(field_bits));
        bool valid = false; target->set(field.property, values[field_bits], &valid);
        if (valid) ++completed;
        if (!live(job) || !ObjectDB::get_instance(root_id) || !ObjectDB::get_instance(target_id)) return ERR_BUSY;
        if (!valid) return ERR_INVALID_DATA;
    }
    if (constructed) {
        Node *parent = Object::cast_to<Node>(ObjectDB::get_instance(spawner_));
        Node *root = Object::cast_to<Node>(ObjectDB::get_instance(root_id));
        if (!parent || !root || !live(job) || root->get_parent()) return ERR_UNCONFIGURED;
        parent->add_child(root);
        if (!live(job) || !ObjectDB::get_instance(root_id)) return ERR_BUSY;
    }
    const auto finished = queue_.complete(job, completed);
    completion.finished = true;
    return finished ? OK : engine_error(finished.error());
}

Dictionary SuperposSpawnRuntime::project(uint32_t budget) {
    Dictionary result; result["error"] = ERR_BUSY;
    if (!Thread::is_main_thread() || projecting_ || stopping_ || budget == 0 || budget > 64) return result;
    Ref<SuperposSpawnRuntime> keep_alive(this);
    projecting_ = true; struct Guard { bool &value; ~Guard() { value = false; } } guard{projecting_};
    result["error"] = OK;
    uint32_t completed = 0, failed = 0;
    for (uint32_t n = 0; n < budget && !stopping_; ++n) {
        auto job = queue_.take(); if (!job) { if (job.error() != superpos::Error::NotReady) result["error"] = engine_error(job.error()); break; }
        Error error = project_one(*job);
        projecting_root_ = ObjectID{};
        if (error == OK) ++completed;
        else {
            ++failed;
            // This also releases the queue's dispatch barrier after a reentrant
            // callback invalidates the old ticket. No stale row is overwritten.
            (void)queue_.complete(*job, 0, superpos::Error::InvalidArgument);
        }
        // Only after the root's frames have unwound. Its destructor callbacks
        // may reenter; project_pending() still reports Busy until this returns.
        if (deferred_delete_.is_valid()) finish_deferred_delete();
    }
    result["completed"] = completed; result["failed"] = failed; return result;
}

Dictionary SuperposSpawnRuntime::status() const {
    Dictionary result;
    if (!Thread::is_main_thread()) { result["error"] = ERR_BUSY; return result; }
    result["error"] = initialized_ ? OK : ERR_UNCONFIGURED;
    uint32_t canonical = 0, ready = 0, failed = 0;
    for (const auto &row : rows_.span()) if (row.occupied) { canonical += row.canonical_ready; ready += row.scene == ScenePhase::Ready; failed += row.scene == ScenePhase::Failed; }
    result["canonical_ready"] = canonical; result["scene_ready"] = ready; result["scene_failed"] = failed;
    result["stopping"] = stopping_; result["owned_native_bytes"] = int64_t(quota_.total()); result["scene_projection"] = "sequential";
    return result;
}
Error SuperposSpawnRuntime::retry(uint64_t handle, const superpos::ReplicaKey &key) {
    if (!Thread::is_main_thread() || stopping_ || projecting_) return ERR_BUSY;
    for (uint32_t i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        if (row.occupied && row.handle.value == handle && row.key == key) {
            const auto status = queue_.retry(slots_[i].projection);
            return status ? OK : engine_error(status.error());
        }
    }
    return ERR_DOES_NOT_EXIST;
}
Dictionary SuperposSpawnRuntime::projection_status(uint64_t handle, const superpos::ReplicaKey &key) const {
    Dictionary result;
    if (!Thread::is_main_thread()) { result["error"] = ERR_BUSY; return result; }
    for (const auto &row : rows_.span()) {
        if (!row.occupied || row.handle.value != handle || row.key != key) continue;
        static const char *phases[] = {"pending", "ready", "failed", "retiring"};
        result["error"] = OK;
        result["scene_phase"] = phases[unsigned(row.scene)];
        result["completed_fields"] = row.completed_fields;
        result["projection_error"] = unsigned(row.projection_error);
        int64_t revision; std::memcpy(&revision, &row.projected_revision, sizeof(revision));
        result["projected_revision"] = revision;
        result["scene_projection"] = "sequential";
        return result;
    }
    result["error"] = ERR_DOES_NOT_EXIST;
    return result;
}
TypedArray<SuperposReplicaView> SuperposSpawnRuntime::views(uint32_t offset, uint32_t limit) {
    TypedArray<SuperposReplicaView> output;
    if (limit > 64 || !Thread::is_main_thread()) return output;
    Ref<SuperposSession> session(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_)));
    if (session.is_null()) return output;
    for (uint32_t i = offset; i < rows_.size() && output.size() < int(limit); ++i) {
        const auto &row = rows_[i]; if (!row.occupied || !row.canonical_ready) continue;
        auto view = new_native<SuperposReplicaView>(); if (view.is_null()) break;
        SuperposReceiverPublicAccess::initialize(*view.ptr(), *session.ptr(), binding_, row.handle.value, identity(row.key));
        output.append(view);
    }
    return output;
}
void SuperposSpawnRuntime::abandon_spawner() noexcept { spawner_ = ObjectID{}; stopping_ = true; }
superpos::Status SuperposSpawnRuntime::quiesce() noexcept {
    Ref<SuperposSpawnRuntime> keep_alive(this); stopping_ = true;
    for (uint32_t i = 0; i < slots_.size(); ++i) if (slots_[i].used) release(slots_[i].projection);
    return {};
}
}
