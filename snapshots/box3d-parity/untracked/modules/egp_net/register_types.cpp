#include "register_types.h"
#include "egp_net_session.h"
#include "core/object/class_db.h"
void initialize_egp_net_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) GDREGISTER_CLASS(EGPNetSession);
}
void uninitialize_egp_net_module(ModuleInitializationLevel p_level) {}
