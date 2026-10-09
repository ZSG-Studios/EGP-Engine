// SPDX-License-Identifier: MIT
#include "register_types.h"
#include "egp_lite_session.h"
#include "core/object/class_db.h"

void initialize_litenet_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EGPLiteSession);
	}
}

void uninitialize_litenet_module(ModuleInitializationLevel p_level) {
}
