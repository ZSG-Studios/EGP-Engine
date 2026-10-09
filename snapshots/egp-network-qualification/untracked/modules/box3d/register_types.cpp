// SPDX-License-Identifier: MIT
#include "register_types.h"

#include "egp_box3d_world.h"

#include "core/object/class_db.h"

#ifdef EGP_BOX3D_SCENE_BACKEND
#include "scene_backend/misc/box3d_globals.hpp"
#include "scene_backend/objects/box3d_physics_direct_body_state_3d.hpp"
#include "scene_backend/servers/box3d_physics_server_3d.hpp"
#include "scene_backend/spaces/box3d_physics_direct_space_state_3d.hpp"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "servers/physics_3d/physics_server_3d_manager.h"

static PhysicsServer3D *create_box3d_scene_server() {
	// The adapter calls scene callbacks synchronously on the simulation owner.
	ERR_FAIL_COND_V_MSG(bool(GLOBAL_GET("physics/3d/run_on_separate_thread")), nullptr, "Box3D scene adapter currently requires the main physics thread.");
	return memnew(Box3DPhysicsServer3D);
}
#endif

void initialize_box3d_module(ModuleInitializationLevel p_level) {
#ifdef EGP_BOX3D_SCENE_BACKEND
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		box3d_initialize();
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsServer3D);
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsDirectBodyState3D);
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsDirectSpaceState3D);
		PhysicsServer3DManager::get_singleton()->register_server("Box3D Physics", callable_mp_static(&create_box3d_scene_server));
	}
#endif
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EGPBox3DWorld);
	}
}
void uninitialize_box3d_module(ModuleInitializationLevel p_level) {
#ifdef EGP_BOX3D_SCENE_BACKEND
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		box3d_deinitialize();
	}
#endif
}
