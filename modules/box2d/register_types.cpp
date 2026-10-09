// SPDX-License-Identifier: MIT
#include "register_types.h"

#include "bodies/box2d_direct_body_state_2d.h"
#include "box2d_physics_server_2d.h"
#include "box2d_project_settings.h"
#include "egp_box2d_world.h"

#include "servers/physics_2d/physics_server_2d_manager.h"

static PhysicsServer2D *create_box2d_server() {
	if (bool(GLOBAL_GET("physics/2d/run_on_separate_thread"))) {
		WARN_PRINT("Box2D runs physics on the main thread; physics/2d/run_on_separate_thread is ignored.");
	}
	return memnew(Box2DPhysicsServer2D);
}

void initialize_box2d_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EGPBox2DWorld);
		return;
	}
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}
	Box2DProjectSettings::register_settings();
	GDREGISTER_ABSTRACT_CLASS(Box2DDirectBodyState2D);
	GDREGISTER_ABSTRACT_CLASS(Box2DDirectSpaceState2D);
	GDREGISTER_ABSTRACT_CLASS(Box2DPhysicsServer2D);
	PhysicsServer2DManager::get_singleton()->register_server("Box2D Physics", callable_mp_static(&create_box2d_server));
	PhysicsServer2DManager::get_singleton()->set_default_server("Box2D Physics");
}

void uninitialize_box2d_module(ModuleInitializationLevel p_level) {}
