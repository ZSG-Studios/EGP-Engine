// SPDX-License-Identifier: MIT
#include "superpos_managed_reload.h"
#include "superpos_session.h"
#include "superpos_world.h"
#include "core/crypto/crypto_core.h"
#include "core/object/object.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include <array>
#include <cstring>
#include <utility>

namespace {
constexpr uint32_t maximum_sessions = 256;
struct Entry {
    uint64_t identity = 0;
    Ref<SuperposSession> session;
    SuperposManagedReload::Ticket ticket;
    bool claimed = false;
};
std::array<Entry, maximum_sessions> entries;
// Reload entries are engine-owner-only. Their Ref operations can swap Mono
// handles, so they must never execute while the worker retirement lock is held.
Mutex lifecycle_mutex;
SuperposManagedReload::DisposalNode *disposal_head = nullptr, *disposal_tail = nullptr, *live_head = nullptr;
SuperposManagedReload::DisposalNode *native_head = nullptr, *native_tail = nullptr;
uint64_t binding_counter = 0;
Object *probe_object = nullptr;
SuperposManagedReload::BindingProbeHook probe_hook = nullptr;
void *probe_payload = nullptr;
uint64_t epoch_counter = 0;
bool active = false;
bool in_flight = false;
bool quiescing = false;
bool managed_bindings_admitted = true;
SuperposManagedReload::DisposalDiagnostics disposal_stats;

// The caller holds lifecycle_mutex. Compare pointer values without reading a
// possibly retired object. Destruction unlinks its embedded record under this
// same mutex; globally unique generations also prevent address-reuse ABA.
SuperposManagedReload::DisposalNode *find_live(Object *object) {
    for (auto *node = live_head; node; node = node->live_next) {
        if (static_cast<Object *>(node->session) == object) { return node; }
    }
    return nullptr;
}
void cancel_queued(SuperposManagedReload::DisposalNode *node) {
    if (!node->queued) { return; }
    auto **link = &disposal_head;
    SuperposManagedReload::DisposalNode *previous = nullptr;
    while (*link && *link != node) { previous = *link; link = &(*link)->next; }
    if (*link == node) {
        *link = node->next;
        if (disposal_tail == node) { disposal_tail = previous; }
        --disposal_stats.pending;
        ++disposal_stats.drained;
        ++disposal_stats.canceled;
    }
    node->queued = false;
    node->queued_generation = 0;
    node->next = nullptr;
    node->callback = nullptr;
}

// Reload membership and transaction flags are native-owner-only. A Ref
// operation may invoke an instance-binding callback; do not let it recursively
// acquire or publish another reload while the current boundary still owns pins.
bool reload_boundary_busy = false;
bool reload_boundary_invalidated = false;
struct ReloadBoundary {
    ReloadBoundary() { reload_boundary_busy = true; reload_boundary_invalidated = false; }
    ~ReloadBoundary() { reload_boundary_invalidated = false; reload_boundary_busy = false; }
};
struct ReloadQuiescence {
    bool armed = false;
    ~ReloadQuiescence() { if (armed) { quiescing = false; } }
};
struct EntrySnapshot {
    uint64_t identity = 0;
    SuperposSession *session = nullptr;
    SuperposManagedReload::Ticket ticket;
    bool claimed = false;
};
using EntrySnapshots = std::array<EntrySnapshot, maximum_sessions>;
bool same_ticket(const SuperposManagedReload::Ticket &a, const SuperposManagedReload::Ticket &b) {
    return a.epoch == b.epoch && a.nonce == b.nonce && a.session == b.session &&
            a.binding == b.binding && a.world_owner == b.world_owner &&
            a.schema_fingerprint == b.schema_fingerprint && a.authored_fingerprint == b.authored_fingerprint;
}
EntrySnapshots snapshot_entries() {
    EntrySnapshots snapshot{};
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        snapshot[i].identity = entries[i].identity;
        snapshot[i].session = entries[i].session.ptr();
        snapshot[i].ticket = entries[i].ticket;
        snapshot[i].claimed = entries[i].claimed;
    }
    return snapshot;
}
bool same_entries(const EntrySnapshots &snapshot) {
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (entries[i].identity != snapshot[i].identity || entries[i].session.ptr() != snapshot[i].session ||
                entries[i].claimed != snapshot[i].claimed || !same_ticket(entries[i].ticket, snapshot[i].ticket)) { return false; }
    }
    return true;
}
bool current_attempt(uint64_t epoch, const EntrySnapshots &snapshot) {
    return active && in_flight && epoch_counter == epoch && !reload_boundary_invalidated && same_entries(snapshot);
}
Error validate_owner(const Ref<SuperposSession> &session, const SuperposManagedReload::Ticket &ticket) {
    if (session.is_null()) { return ERR_INVALID_DATA; }
    // Exclude both registry retention and this operation's independent pin.
    // Neither can stand in for the ordinary native/managed owner being rebound.
    if (session->get_reference_count() <= 2) { return ERR_UNAVAILABLE; }
    if (ticket.world_owner) {
        const ObjectID identity(ticket.world_owner);
        auto *world = Object::cast_to<SuperposWorld>(ObjectDB::get_instance(identity));
        if (!world) { return ERR_UNAVAILABLE; }
        uint64_t attached_identity = 0;
        const Error checked = world->query_session_identity(attached_identity);
        if (checked != OK || ObjectDB::get_instance(identity) != world || attached_identity != uint64_t(session->get_instance_id())) { return ERR_UNAVAILABLE; }
    }
    return OK;
}
}

Error SuperposManagedReload::track(SuperposSession *p_session) {
    if (!Thread::is_main_thread()) { return ERR_UNAVAILABLE; }
    if (p_session->owner_retired) { return ERR_UNAVAILABLE; }
    if (active || reload_boundary_busy) { return ERR_BUSY; }
    for (const auto &entry : entries) {
        if (entry.identity == uint64_t(p_session->get_instance_id())) {
            MutexLock lock(lifecycle_mutex);
            p_session->managed_disposal.reload_tracked = true;
            return OK;
        }
    }
    for (auto &entry : entries) {
        if (!entry.identity) {
            entry.identity = uint64_t(p_session->get_instance_id());
            MutexLock lock(lifecycle_mutex);
            p_session->managed_disposal.reload_tracked = true;
            return OK;
        }
    }
    return ERR_OUT_OF_MEMORY;
}
void SuperposManagedReload::forget(SuperposSession *p_session) {
    // Supported managed destruction is drained on the same owner as weak
    // lookup+reference acquisition. Native last-reference release is also an
    // owner-thread obligation; this mutex cannot legalize worker destruction.
    ERR_FAIL_COND(Thread::get_caller_id() != p_session->owner_thread);
    if (!Thread::is_main_thread()) {
        // A native worker may construct an unconfigured Resource which never
        // entered the main-owner reload registry or a managed binding. Rejecting
        // configure() must still permit its ordinary creator-thread teardown.
        MutexLock lock(lifecycle_mutex);
        ERR_FAIL_COND(p_session->managed_disposal.reload_tracked);
        return;
    }
    {
        MutexLock lock(lifecycle_mutex);
        p_session->managed_disposal.reload_tracked = false;
    }
    for (auto &entry : entries) {
        if (entry.identity == uint64_t(p_session->get_instance_id())) {
            // Removing one admitted participant invalidates the whole
            // attempt. Remaining native participants stay paused until
            // an ordinary fresh retry; stale capsules cannot complete.
            if (active) { in_flight = false; }
            if (reload_boundary_busy) { reload_boundary_invalidated = true; }
            entry = Entry{};
            return;
        }
    }
}
void SuperposManagedReload::on_construct(SuperposSession *p_session) {
    MutexLock lock(lifecycle_mutex);
    auto &node = p_session->managed_disposal;
    node.session = p_session;
    node.live_next = live_head;
    if (live_head) { live_head->live_previous = &node; }
    live_head = &node;
}
void SuperposManagedReload::on_destruct(SuperposSession *p_session) {
    {
        MutexLock lock(lifecycle_mutex);
        auto &node = p_session->managed_disposal;
        CRASH_COND(node.native_queued || node.native_releases);
        cancel_queued(&node);
        if (node.live_previous) { node.live_previous->live_next = node.live_next; }
        else { live_head = node.live_next; }
        if (node.live_next) { node.live_next->live_previous = node.live_previous; }
        if (p_session->managed_disposal.was_queued) {
            ++disposal_stats.destructed;
            if (!Thread::is_main_thread()) { ++disposal_stats.wrong_thread_destructed; }
        }
    }
    // Registry Ref release can reach managed handles; never do it while the
    // retirement mutex is held. Unadmitted worker objects touch no entries.
    forget(p_session);
}
bool SuperposManagedReload::is_active() { return Thread::is_main_thread() && active; }
bool SuperposManagedReload::is_quiescing() { return Thread::is_main_thread() && quiescing; }
Error SuperposManagedReload::revoke_managed_bindings(uint64_t p_epoch, RevokeBindingCallback p_callback) {
    if (!Thread::is_main_thread() || !p_callback || reload_boundary_busy || quiescing) { return ERR_UNAVAILABLE; }
    ReloadBoundary boundary;
    ReloadQuiescence quiescence;
    drain_managed_disposals();
    if (!active || !in_flight || p_epoch != epoch_counter) { return ERR_UNAVAILABLE; }
    reload_boundary_invalidated = false;
    const EntrySnapshots snapshot = snapshot_entries();
    std::array<Ref<SuperposSession>, maximum_sessions> retained;
    // Registry entries can be removed by any later lifecycle callback. Retain
    // the entire candidate set independently before invoking even the first.
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (!snapshot[i].session) { continue; }
        retained[i] = entries[i].session;
        if (!current_attempt(p_epoch, snapshot) || retained[i].ptr() != snapshot[i].session || retained[i]->owner_retired) {
            failed(p_epoch); return ERR_UNAVAILABLE;
        }
    }
    struct Candidate { SuperposSession *session = nullptr; uint64_t generation = 0; };
    std::array<Candidate, maximum_sessions> candidates{};
    uint32_t count = 0;
    {
        MutexLock lock(lifecycle_mutex);
        if (!current_attempt(p_epoch, snapshot)) { return ERR_UNAVAILABLE; }
        for (uint32_t i = 0; i < maximum_sessions; ++i) {
            if (retained[i].is_null()) { continue; }
            auto *node = find_live(retained[i].ptr());
            if (!node) { return ERR_INVALID_DATA; }
            if (!node->generation) { continue; }
            if (!node->counted || node->queued || node->retiring) { return ERR_BUSY; }
            candidates[count++] = {retained[i].ptr(), node->generation};
        }
        quiescing = true;
        quiescence.armed = true;
    }
    Error outcome = OK;
    for (uint32_t i = 0; i < count; ++i) {
        if (!current_attempt(p_epoch, snapshot) || candidates[i].session->owner_retired) { outcome = ERR_UNAVAILABLE; break; }
        const Error revoked = p_callback(candidates[i].session, candidates[i].generation);
        if (!current_attempt(p_epoch, snapshot) || candidates[i].session->owner_retired) { outcome = ERR_UNAVAILABLE; break; }
        {
            MutexLock lock(lifecycle_mutex);
            auto *node = find_live(candidates[i].session);
            if (revoked != OK || !node || node->generation || node->counted || node->queued || node->retiring) {
                outcome = revoked == OK ? ERR_INVALID_DATA : revoked;
            }
        }
        if (outcome != OK) { break; }
    }
    if (outcome != OK) { failed(p_epoch); }
    // retained is released before quiescence/boundary. No callback can create
    // a replacement binding or recursively commit during the old pin cleanup.
    return outcome;
}
Error SuperposManagedReload::begin(uint64_t &r_epoch) {
    r_epoch = 0;
    if (!Thread::is_main_thread() || reload_boundary_busy || quiescing) { return ERR_UNAVAILABLE; }
    ReloadBoundary boundary;
    drain_managed_disposals();
    if (in_flight) { return ERR_BUSY; }
    if (epoch_counter == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
    reload_boundary_invalidated = false;
    const uint64_t previous_epoch = epoch_counter;
    const bool previous_active = active;
    const uint64_t next = previous_epoch + 1;
    const EntrySnapshots snapshot = snapshot_entries();
    std::array<Ticket, maximum_sessions> proposed{};
    std::array<Ref<SuperposSession>, maximum_sessions> displaced;
    std::array<Ref<SuperposSession>, maximum_sessions> retained;
    const auto unchanged = [&]() {
        return !reload_boundary_invalidated && !in_flight && active == previous_active &&
                epoch_counter == previous_epoch && same_entries(snapshot);
    };
    // Acquisition and authored catalog checks may enter lifecycle callbacks.
    // Keep pins independent and reject any membership/state change afterward.
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (!snapshot[i].identity) { continue; }
        retained[i] = entries[i].session;
        if (!unchanged()) { return ERR_UNAVAILABLE; }
        if (retained[i].is_null()) {
            auto *native = Object::cast_to<SuperposSession>(ObjectDB::get_instance(ObjectID(snapshot[i].identity)));
            retained[i] = Ref<SuperposSession>(native);
        }
        if (!unchanged() || retained[i].is_null() || retained[i]->owner_retired ||
                uint64_t(retained[i]->get_instance_id()) != snapshot[i].identity) { return ERR_UNAVAILABLE; }
        const Error check = retained[i]->_reload_preflight();
        if (!unchanged() || retained[i]->owner_retired) { return ERR_UNAVAILABLE; }
        if (check != OK) { return check; }
        auto &ticket = proposed[i];
        ticket.epoch = next;
        ticket.session = snapshot[i].identity;
        uint64_t binding = 0;
        const Error binding_error = retained[i]->query_binding_generation(binding);
        if (binding_error != OK) { return binding_error; }
        if (binding == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
        ticket.binding = binding + 1;
        ticket.world_owner = retained[i]->reload_world_owner;
        ticket.schema_fingerprint = retained[i]->_reload_schema_fingerprint();
        if (!unchanged() || retained[i]->owner_retired) { return ERR_UNAVAILABLE; }
        ticket.authored_fingerprint = retained[i]->_reload_authored_fingerprint();
        if (!unchanged() || retained[i]->owner_retired) { return ERR_UNAVAILABLE; }
        if (ticket.schema_fingerprint.is_empty() || ticket.authored_fingerprint.is_empty()) { return ERR_INVALID_DATA; }
        if (ticket.world_owner) {
            const ObjectID identity(ticket.world_owner);
            auto *world = Object::cast_to<SuperposWorld>(ObjectDB::get_instance(identity));
            if (!world) { return ERR_UNAVAILABLE; }
            Ref<SuperposSession> attached;
            const Error attached_error = world->query_session(attached);
            if (!unchanged() || retained[i]->owner_retired || attached_error != OK ||
                    ObjectDB::get_instance(identity) != world || attached.ptr() != retained[i].ptr()) { return ERR_UNAVAILABLE; }
        }
        bool unique = false;
        for (int attempt = 0; attempt < 8 && !unique; ++attempt) {
            if (CryptoCore::generate_random(reinterpret_cast<uint8_t *>(&ticket.nonce), sizeof(ticket.nonce)) != OK) { return ERR_UNAVAILABLE; }
            unique = ticket.nonce != 0;
            for (uint32_t prior = 0; prior < i && unique; ++prior) { unique = proposed[prior].nonce != ticket.nonce; }
        }
        if (!unique) { return ERR_UNAVAILABLE; }
        if (!unchanged() || retained[i]->owner_retired) { return ERR_UNAVAILABLE; }
    }
    if (!unchanged()) { return ERR_UNAVAILABLE; }
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (retained[i].is_valid() && retained[i]->owner_retired) { return ERR_UNAVAILABLE; }
    }
    // Native linearization: this entire publication/pause loop is callback-free.
    // Empty move destinations prevent Ref increment/drop callbacks at commit.
    epoch_counter = next;
    active = in_flight = true;
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (retained[i].is_null()) { continue; }
        displaced[i] = std::move(entries[i].session);
        entries[i].session = std::move(retained[i]);
        entries[i].ticket = proposed[i];
        entries[i].claimed = false;
        entries[i].session->_reload_pause(next);
    }
    r_epoch = next;
    return OK;
}
uint32_t SuperposManagedReload::ticket_count() {
    if (!Thread::is_main_thread() || !active || !in_flight) { return 0; }
    uint32_t count = 0;
    for (const auto &entry : entries) { count += entry.session.is_valid() ? 1 : 0; }
    return count;
}
Error SuperposManagedReload::ticket(uint32_t p_index, Ticket &r_ticket) {
    if (!Thread::is_main_thread() || !active || !in_flight) { return ERR_UNAVAILABLE; }
    uint32_t ordinal = 0;
    for (const auto &entry : entries) {
        if (entry.session.is_null()) { continue; }
        if (ordinal++ == p_index) { r_ticket = entry.ticket; return OK; }
    }
    return ERR_INVALID_PARAMETER;
}
Error SuperposManagedReload::claim(const Ticket &p_ticket) {
    if (!Thread::is_main_thread() || reload_boundary_busy || quiescing || !active || !in_flight) { return ERR_UNAVAILABLE; }
    ReloadBoundary boundary;
    const uint64_t epoch = epoch_counter;
    const Ticket request = p_ticket;
    const EntrySnapshots snapshot = snapshot_entries();
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (!snapshot[i].session || snapshot[i].ticket.session != request.session) { continue; }
        const Ticket expected = snapshot[i].ticket;
        if (snapshot[i].claimed || !same_ticket(request, expected)) { return ERR_INVALID_PARAMETER; }
        Ref<SuperposSession> retained = entries[i].session;
        if (!current_attempt(epoch, snapshot) || retained.ptr() != snapshot[i].session || retained->owner_retired) { return ERR_UNAVAILABLE; }
        const Error owner = validate_owner(retained, expected);
        if (!current_attempt(epoch, snapshot) || retained->owner_retired || owner != OK) { return ERR_UNAVAILABLE; }
        const Error validated = retained->_reload_validate(expected);
        if (!current_attempt(epoch, snapshot) || retained->owner_retired || retained->get_reference_count() <= 2 || validated != OK) { return ERR_UNAVAILABLE; }
        // Claim publication has no native reference or gameplay callback.
        entries[i].claimed = true;
        return OK;
    }
    return ERR_INVALID_PARAMETER;
}
Error SuperposManagedReload::complete(uint64_t p_epoch, bool p_callbacks_validated) {
    if (!Thread::is_main_thread() || reload_boundary_busy || quiescing || !active || !in_flight || p_epoch != epoch_counter) { return ERR_INVALID_PARAMETER; }
    ReloadBoundary boundary;
    if (!p_callbacks_validated) { failed(p_epoch); return ERR_UNAVAILABLE; }
    const EntrySnapshots snapshot = snapshot_entries();
    std::array<Ref<SuperposSession>, maximum_sessions> released;
    std::array<Ref<SuperposSession>, maximum_sessions> retained;
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (!snapshot[i].session) { continue; }
        retained[i] = entries[i].session;
        if (!current_attempt(p_epoch, snapshot) || retained[i].ptr() != snapshot[i].session || retained[i]->owner_retired) {
            failed(p_epoch); return ERR_UNAVAILABLE;
        }
    }
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (retained[i].is_null()) { continue; }
        if (!snapshot[i].claimed) { failed(p_epoch); return ERR_UNAVAILABLE; }
        const Error owner = validate_owner(retained[i], snapshot[i].ticket);
        if (!current_attempt(p_epoch, snapshot) || retained[i]->owner_retired || owner != OK) { failed(p_epoch); return ERR_UNAVAILABLE; }
        const Error validated = retained[i]->_reload_validate(snapshot[i].ticket);
        if (!current_attempt(p_epoch, snapshot) || retained[i]->owner_retired || validated != OK) { failed(p_epoch); return ERR_UNAVAILABLE; }
    }
    if (!current_attempt(p_epoch, snapshot)) { failed(p_epoch); return ERR_UNAVAILABLE; }
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (retained[i].is_valid() && (retained[i]->owner_retired || retained[i]->get_reference_count() <= 2)) { failed(p_epoch); return ERR_UNAVAILABLE; }
    }
    // Native linearization: no Ref operations, authored reads, transport pumps,
    // or callbacks occur during resume and registry-retention detachment.
    for (uint32_t i = 0; i < maximum_sessions; ++i) {
        if (retained[i].is_valid()) { retained[i]->_reload_resume(p_epoch); }
        entries[i].ticket = Ticket{};
        entries[i].claimed = false;
        released[i] = std::move(entries[i].session);
    }
    active = in_flight = false;
    // Old registry references are now local. Post-commit lifecycle callbacks
    // cannot release a later transaction's retention through mutable entries.
    return OK;
}
void SuperposManagedReload::failed(uint64_t p_epoch) {
    if (Thread::is_main_thread() && active && p_epoch == epoch_counter) { in_flight = false; }
    // Retain the exact native state and transport. A later native retry issues
    // fresh tickets; there is no implicit resume after failure/schema change.
}
void SuperposManagedReload::shutdown() {
    if (!Thread::is_main_thread()) { return; }
    if (reload_boundary_busy) { reload_boundary_invalidated = true; }
    drain_managed_disposals();
    active = in_flight = false;
    auto retained = entries;
    for (auto &entry : entries) { entry = Entry{}; }
    for (auto &entry : retained) {
        if (entry.session.is_null() && entry.identity) {
            entry.session = Ref<SuperposSession>(Object::cast_to<SuperposSession>(ObjectDB::get_instance(ObjectID(entry.identity))));
        }
        if (entry.session.is_valid()) { entry.session->_reload_abort_close(); }
    }
}

Error SuperposManagedReload::claim_managed_binding(Object *p_object, uint64_t &r_generation) {
    r_generation = 0;
    if (!can_create_managed()) { return ERR_UNAVAILABLE; }
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(p_object);
    if (!node) { return ERR_DOES_NOT_EXIST; }
    if (Thread::get_caller_id() != node->session->owner_thread || node->session->owner_retired) { return ERR_UNAVAILABLE; }
    if (node->generation || node->queued || node->counted || node->retiring) { return ERR_BUSY; }
    if (binding_counter == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
    node->generation = r_generation = ++binding_counter;
    node->fresh_native = !node->session->is_referenced() && node->session->get_reference_count() == 1;
    return OK;
}
Error SuperposManagedReload::validate_managed_claim(Object *object, uint64_t generation) {
    if (!can_create_managed() || !generation) { return ERR_UNAVAILABLE; }
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(object);
    if (!node) { return ERR_DOES_NOT_EXIST; }
    if (Thread::get_caller_id() != node->session->owner_thread || node->session->owner_retired) { return ERR_UNAVAILABLE; }
    if (node->generation != generation || node->queued || node->retiring) { return ERR_BUSY; }
    return OK;
}
bool SuperposManagedReload::is_owner_retired(Object *object) {
    if (!Thread::is_main_thread()) { return false; }
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(object);
    return node && node->session->owner_retired;
}
void SuperposManagedReload::managed_reference_added(Object *p_object) {
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(p_object);
    if (!node) { return; }
    ERR_FAIL_COND_MSG(!Thread::is_main_thread() || !node->generation || node->counted || node->retiring,
            "Superpos unsafe managed reference requires an owner-issued uncounted binding claim.");
    node->counted = true;
    node->fresh_native = false;
}
void SuperposManagedReload::managed_reference_removed(Object *p_object) {
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(p_object);
    if (!node) { return; }
    ERR_FAIL_COND_MSG(!Thread::is_main_thread() || !node->counted,
            "Superpos managed reference may be consumed once on its native owner.");
    // Script-side transfer uses its existing unsafe-unref, not an extra drop.
    // Cancel the old queued node before that drop can destroy the native object.
    cancel_queued(node);
    node->counted = false;
    node->retiring = true;
}
void SuperposManagedReload::managed_binding_released(Object *p_object) {
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(p_object);
    if (!node || node->counted || node->queued) { return; }
    node->generation = 0;
    node->retiring = false;
}
bool SuperposManagedReload::is_retirement_callback(Object *p_object) {
    if (!Thread::is_main_thread()) { return false; }
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(p_object);
    return node && node->retiring && node->counted;
}
Error SuperposManagedReload::retire_managed_binding(Object *p_object, uint64_t p_generation,
        bool p_finalizer, DisposalCallback p_callback) {
    if (!p_generation || !p_callback) { return ERR_INVALID_PARAMETER; }
    bool owner = Thread::is_main_thread();
    SuperposSession *uncounted_cleanup = nullptr;
    {
        MutexLock lock(lifecycle_mutex);
        auto *node = find_live(p_object);
        // A script transfer may already have consumed the old counted ref.
        // Do not inspect an unregistered object or a different generation.
        if (!node || node->generation != p_generation) {
            ++disposal_stats.already_retired;
            return ERR_ALREADY_IN_USE;
        }
        if (node->queued || node->retiring) { return ERR_ALREADY_IN_USE; }
        if (!node->counted && !node->fresh_native) {
            node->generation = 0; // Existing owner retains the uncounted object.
            return OK;
        }
        if (owner && !node->counted) {
            uncounted_cleanup = node->session;
            node->generation = 0;
        }
        node->retiring = owner;
        if (!owner) {
            node->callback = p_callback;
            node->queued_generation = p_generation;
            node->finalizer = p_finalizer;
            node->next = nullptr;
            node->queued = node->was_queued = true;
            ++disposal_stats.accepted;
            if (p_finalizer) { ++disposal_stats.finalizers; }
            else { ++disposal_stats.explicit_workers; }
            ++disposal_stats.pending;
            if (disposal_stats.pending > disposal_stats.peak) { disposal_stats.peak = disposal_stats.pending; }
            if (disposal_tail) { disposal_tail->next = node; }
            else { disposal_head = node; }
            disposal_tail = node;
            return OK;
        }
    }
    // No lifecycle lock is held across GCHandle access/managed callbacks.
    // Existing counted ownership keeps this object alive through this call.
    if (uncounted_cleanup) {
        // NativeCtor's initial, unreferenced object: abort without an unsafe unref.
        memdelete(uncounted_cleanup);
        return OK;
    }
    p_callback(p_object, nullptr, p_finalizer);
    managed_binding_released(p_object); // Pointer-only lookup if it was freed.
    return OK;
}
Error SuperposManagedReload::protect_script_change(Object *object, Ref<RefCounted> &pin) {
    auto *session = Object::cast_to<SuperposSession>(object);
    if (!session) { return OK; }
    // The setter receives an already owned live object. Ref callbacks are
    // admitted only on its creator owner, before acquiring the lifetime pin.
    if (Thread::get_caller_id() != session->owner_thread) { return ERR_BUSY; }
    pin = Ref<RefCounted>(session);
    return pin.is_valid() ? OK : ERR_UNCONFIGURED;
}
Error SuperposManagedReload::prepare_managed_binding(Object *p_object) {
    auto *session = Object::cast_to<SuperposSession>(p_object);
    if (!session) { return OK; }
    if (!can_create_managed() || Thread::get_caller_id() != session->owner_thread || session->owner_retired) { return ERR_UNAVAILABLE; }
    drain_managed_disposals();
    if (!can_create_managed() || session->owner_retired) { return ERR_UNAVAILABLE; }
    BindingProbeHook hook = nullptr;
    void *payload = nullptr;
    {
        MutexLock lock(lifecycle_mutex);
        if (probe_object == p_object) {
            hook = probe_hook;
            payload = probe_payload;
            probe_object = nullptr;
            probe_hook = nullptr;
            probe_payload = nullptr;
        }
    }
    if (hook) { hook(p_object, payload); }
    if (!can_create_managed() || session->owner_retired) { return ERR_UNAVAILABLE; }
    return OK;
}
Error SuperposManagedReload::set_binding_probe_hook(Object *p_object, BindingProbeHook p_hook, void *p_payload) {
    if (!can_create_managed() || !p_hook) { return ERR_UNAVAILABLE; }
    MutexLock lock(lifecycle_mutex);
    if (probe_hook || !find_live(p_object)) { return ERR_BUSY; }
    probe_object = p_object;
    probe_hook = p_hook;
    probe_payload = p_payload;
    return OK;
}
bool SuperposManagedReload::can_create_managed() {
    return Thread::is_main_thread() && managed_bindings_admitted && !quiescing;
}
void SuperposManagedReload::begin_managed_shutdown() {
    if (Thread::is_main_thread()) { managed_bindings_admitted = false; }
}
SuperposManagedReload::DisposalDiagnostics SuperposManagedReload::disposal_diagnostics() {
    MutexLock lock(lifecycle_mutex);
    return disposal_stats;
}
Error SuperposManagedReload::defer_native_reference(Object *object, bool &deferred) {
    deferred = false;
    if (!Object::cast_to<SuperposSession>(object)) { return OK; }
    MutexLock lock(lifecycle_mutex);
    auto *node = find_live(object);
    if (!node || Thread::get_caller_id() == node->session->owner_thread) { return OK; }
    // Configured/managed Sessions are main-owner objects. An unconfigured
    // creator-worker Resource can still be destroyed normally on that creator.
    if (node->session->owner_thread != Thread::get_main_id()) { return ERR_UNAVAILABLE; }
    // No valid set of uint32 native references can exhaust this uint64 field.
    // Reject corrupted bookkeeping before consuming a reference.
    if (node->native_releases >= UINT32_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
    ++node->native_releases;
    ++disposal_stats.native_accepted;
    ++disposal_stats.native_pending;
    if (disposal_stats.native_pending > disposal_stats.native_peak) {
        disposal_stats.native_peak = disposal_stats.native_pending;
    }
    if (!node->native_queued) {
        node->native_next = nullptr;
        node->native_queued = true;
        node->was_queued = true;
        if (native_tail) { native_tail->native_next = node; }
        else { native_head = node; }
        native_tail = node;
    }
    deferred = true;
    return OK;
}
void SuperposManagedReload::drain_native_references() {
    if (!Thread::is_main_thread()) { return; }
    while (true) {
        SuperposSession *session = nullptr;
        uint64_t count = 0;
        {
            MutexLock lock(lifecycle_mutex);
            if (!native_head) { return; }
            auto *node = native_head;
            native_head = node->native_next;
            if (!native_head) { native_tail = nullptr; }
            node->native_next = nullptr;
            node->native_queued = false;
            session = node->session;
            count = node->native_releases;
            node->native_releases = 0;
        }
        // Each accepted drop still exists in the native count, keeping the
        // object alive until this owner applies that exact drop. New concurrent
        // worker transfers enqueue independently and keep their own counts.
        for (uint64_t i = 0; i < count; ++i) {
            {
                MutexLock lock(lifecycle_mutex);
                ++disposal_stats.native_drained;
                --disposal_stats.native_pending;
            }
            if (session->unreference()) {
                CRASH_COND(i + 1 != count);
                memdelete(session);
                break;
            }
        }
    }
}
void SuperposManagedReload::drain_managed_disposals() {
    if (!Thread::is_main_thread()) { return; }
    while (true) {
        DisposalCallback callback = nullptr;
        SuperposSession *session = nullptr;
        bool finalizer = false, uncounted_cleanup = false;
        {
            MutexLock lock(lifecycle_mutex);
            if (!disposal_head) { break; }
            auto *node = disposal_head;
            disposal_head = node->next;
            if (!disposal_head) { disposal_tail = nullptr; }
            if (node->generation == node->queued_generation && (node->counted || node->fresh_native) && !node->retiring) {
                callback = node->callback;
                session = node->session;
                finalizer = node->finalizer;
                uncounted_cleanup = !node->counted;
                node->retiring = true;
            }
            node->queued = false;
            node->queued_generation = 0;
            node->callback = nullptr;
            node->next = nullptr;
            --disposal_stats.pending;
            ++disposal_stats.drained;
        }
        if (callback && uncounted_cleanup) {
            memdelete(session);
        } else if (callback) {
            // Exact old counted reference and its handle retire on the owner.
            callback(session, nullptr, finalizer);
            managed_binding_released(session); // Never dereference after delete.
        }
    }
    drain_native_references();
}
