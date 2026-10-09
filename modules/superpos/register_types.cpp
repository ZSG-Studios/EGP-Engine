// SPDX-License-Identifier: MIT
#include "register_types.h"
#ifdef SUPERPOS_HAS_RTC
#include "rtc_process.hpp"
#endif
#include "superpos_schema.h"
#include "superpos_session.h"
#include "private/lifecycle_engine/staged/native_receiver_access.hpp"
#ifdef SUPERPOS_LIFECYCLE_FIXTURE
void superpos_register_lifecycle_fixture();
void superpos_register_public_spawner_fixture();
#endif
#include "superpos_simulation_provider.h"
#include "superpos_uint64.h"
#include "superpos_world.h"
#include "superpos_replicator.h"
#include "superpos_spawner.h"
#include "superpos_lockstep.h"
#include "private/spawning/spawn_runtime.hpp"
#include "superpos_managed_reload.h"
#include "core/object/class_db.h"
#ifdef TOOLS_ENABLED
#include "superpos_uint64_editor.h"
#endif

void initialize_superpos_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        initialize_superpos_uint64_inspector();
        return;
    }
#endif
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) { return; }
#ifdef SUPERPOS_HAS_RTC
    superpos_egp::rtc_process_register_idle();
#endif
    GDREGISTER_CLASS(SuperposUInt64);
    GDREGISTER_CLASS(SuperposField);
    GDREGISTER_CLASS(SuperposSchema);
    GDREGISTER_ABSTRACT_CLASS(SuperposSimulationProvider);
    GDREGISTER_ABSTRACT_CLASS(superpos_egp::lifecycle_engine::SuperposNativeFactory);
    GDREGISTER_CLASS(SuperposSpawnEntry);
    GDREGISTER_CLASS(SuperposSpawnCatalog);
    GDREGISTER_ABSTRACT_CLASS(SuperposReplicaView);
    ClassDB::register_internal_class<superpos_egp::spawning::SuperposSpawnProxy>();
    ClassDB::register_internal_class<superpos_egp::spawning::SuperposSpawnRuntime>();
    ClassDB::register_internal_class<superpos_egp::spawning::SuperposSpawnFactory>();
    GDREGISTER_CLASS(SuperposSession);
    GDREGISTER_CLASS(SuperposWorld);
    GDREGISTER_CLASS(SuperposReplicator);
    GDREGISTER_CLASS(SuperposSpawner);
    GDREGISTER_CLASS(SuperposLockstepClient);
    GDREGISTER_CLASS(SuperposLockstepServer);
#ifdef SUPERPOS_LIFECYCLE_FIXTURE
    superpos_register_lifecycle_fixture();
    superpos_register_public_spawner_fixture();
#endif
#ifdef SUPERPOS_RTC_EMBEDDED_FIXTURE
    superpos_egp_register_embedded_fixture();
#endif
}
void uninitialize_superpos_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        SuperposManagedReload::shutdown();
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
        const auto native_shutdown=SuperposNativeReceiverAccess::shutdown();
        CRASH_COND_MSG(native_shutdown!=OK,"Native factory shutdown failed to settle reserved retirement custody.");
#endif
#ifdef SUPERPOS_HAS_RTC
        superpos_egp::rtc_process_shutdown();
#endif
    }
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) { uninitialize_superpos_uint64_inspector(); }
#endif
}
