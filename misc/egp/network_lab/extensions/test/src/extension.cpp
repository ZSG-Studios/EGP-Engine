#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

class EGP_test_Node : public Node {
    GDCLASS(EGP_test_Node, Node);

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_message"), &EGP_test_Node::get_message);
    }

public:
    String get_message() const { return "Hello from test!"; }
};

static void initialize_extension(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(EGP_test_Node);
    }
}

static void terminate_extension(ModuleInitializationLevel level) {}

extern "C" GDExtensionBool GDE_EXPORT test_init(
        GDExtensionInterfaceGetProcAddress get_proc_address,
        GDExtensionClassLibraryPtr library,
        GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_extension);
    init.register_terminator(terminate_extension);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
