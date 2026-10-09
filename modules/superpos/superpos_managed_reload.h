// SPDX-License-Identifier: MIT
#pragma once
#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include <cstdint>

class SuperposSession;
class Object;
class RefCounted;
template <typename T> class Ref;

// Private native lifecycle integration. No GDScript helper or public ClassDB
// registration is involved. Tickets are owner-issued and never sent on wire.
class SuperposManagedReload {
public:
    using DisposalCallback = void (*)(Object *, void *, bool);
    // Reserved in each native Session before a managed binding is admitted.
    // No allocation or queue-capacity failure is possible at retirement.
    struct DisposalNode {
        SuperposSession *session = nullptr;
        DisposalCallback callback = nullptr;
        DisposalNode *next = nullptr;
        DisposalNode *live_next = nullptr, *live_previous = nullptr;
        uint64_t generation = 0, queued_generation = 0;
        bool counted = false, retiring = false, fresh_native = false;
        bool finalizer = false, queued = false, was_queued = false;
        bool reload_tracked = false;
        // A worker transfers its existing reference here without decrementing
        // or allocating. uint64 exceeds RefCounted's entire uint32 count domain.
        uint64_t native_releases = 0;
        DisposalNode *native_next = nullptr;
        bool native_queued = false;

    };
    struct Ticket {
        uint64_t epoch = 0;
        uint64_t nonce = 0;
        uint64_t session = 0;
        uint64_t binding = 0;
        uint64_t world_owner = 0;
        String schema_fingerprint;
        String authored_fingerprint;
    };
    static Error track(SuperposSession *p_session);
    static void forget(SuperposSession *p_session);
    static void on_destruct(SuperposSession *p_session);
    static void on_construct(SuperposSession *p_session);
    static bool is_active();
    static Error begin(uint64_t &r_epoch);
    using RevokeBindingCallback = Error (*)(Object *, uint64_t);
    static Error revoke_managed_bindings(uint64_t, RevokeBindingCallback);
    static bool is_quiescing();
    static uint32_t ticket_count();
    static Error ticket(uint32_t p_index, Ticket &r_ticket);
    static Error claim(const Ticket &p_ticket);
    // This receipt comes from the trusted native Mono bridge, not a script.
    static Error complete(uint64_t p_epoch, bool p_callbacks_validated);
    static void failed(uint64_t p_epoch);
    static void shutdown();
    // ERR_SKIP delegates ordinary resources/main-thread disposals to EGP.
    static Error claim_managed_binding(Object *, uint64_t &);
    static Error validate_managed_claim(Object *, uint64_t);
    static bool is_owner_retired(Object *);
    static Error retire_managed_binding(Object *, uint64_t, bool, DisposalCallback);
    static void managed_reference_added(Object *);
    static void managed_reference_removed(Object *);
    static void managed_binding_released(Object *);
    static bool is_retirement_callback(Object *);
    // Isolated native qualification driver: a one-use hook, never ClassDB.
    using BindingProbeHook = void (*)(Object *, void *);
    static Error set_binding_probe_hook(Object *, BindingProbeHook, void *);
    static Error prepare_managed_binding(Object *);
    static Error protect_script_change(Object *, Ref<RefCounted> &);
    static void drain_managed_disposals();
    static Error defer_native_reference(Object *, bool &);
    static void drain_native_references();

    static bool can_create_managed();
    static void begin_managed_shutdown();
    struct DisposalDiagnostics {
        uint64_t accepted = 0, finalizers = 0, explicit_workers = 0;
        uint64_t drained = 0, destructed = 0, wrong_thread_destructed = 0;
        uint64_t pending = 0, peak = 0, already_retired = 0, canceled = 0;
        uint64_t native_accepted = 0, native_drained = 0, native_pending = 0, native_peak = 0;

    };
    static DisposalDiagnostics disposal_diagnostics();
};
