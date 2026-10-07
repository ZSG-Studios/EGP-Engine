/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "egp_box3d_world.h"
#include "scene_backend/misc/box3d_globals.hpp"
#include "scene_backend/objects/box3d_physics_direct_body_state_3d.hpp"
#include "scene_backend/servers/box3d_physics_server_3d.hpp"
#include "scene_backend/spaces/box3d_physics_direct_space_state_3d.hpp"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/physics_3d/physics_server_3d_manager.h"

static PhysicsServer3D *create_box3d_scene_server() {
	// The adapter calls scene callbacks synchronously on the simulation owner.
	if (bool(GLOBAL_GET("physics/3d/run_on_separate_thread"))) {
		WARN_PRINT("Box3D runs physics on the main thread; physics/3d/run_on_separate_thread is ignored.");
	}
	return memnew(Box3DPhysicsServer3D);
}

void initialize_box3d_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		box3d_initialize();
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsServer3D);
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsDirectBodyState3D);
		GDREGISTER_ABSTRACT_CLASS(Box3DPhysicsDirectSpaceState3D);
		PhysicsServer3DManager::get_singleton()->register_server(PhysicsServer3DManager::BOX3D_PHYSICS_NAME, callable_mp_static(&create_box3d_scene_server));
		PhysicsServer3DManager::get_singleton()->set_default_server(PhysicsServer3DManager::BOX3D_PHYSICS_NAME);
	}
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GLOBAL_DEF("physics/box3d/audit_determinism", false);
		GDREGISTER_CLASS(EGPBox3DWorld);
	}
}
void uninitialize_box3d_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		box3d_deinitialize();
	}
}
