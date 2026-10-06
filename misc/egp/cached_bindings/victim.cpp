#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
using namespace godot;

class EGPBindingVictim : public Node {
	GDCLASS(EGPBindingVictim, Node);
	int64_t counter = 17;

protected:
	static void _bind_methods() {
#if BINDING_VERSION == 4
		ClassDB::bind_method(D_METHOD("read_value", "offset"), &EGPBindingVictim::read_value);
		ClassDB::bind_static_method("EGPBindingVictim", D_METHOD("read_static", "offset"), &EGPBindingVictim::read_static);
#else
		ClassDB::bind_method(D_METHOD("read_value"), &EGPBindingVictim::read_value);
		ClassDB::bind_static_method("EGPBindingVictim", D_METHOD("read_static"), &EGPBindingVictim::read_static);
#endif
		ClassDB::bind_method(D_METHOD("get_counter"), &EGPBindingVictim::get_counter);
		ClassDB::bind_method(D_METHOD("read_text"), &EGPBindingVictim::read_text);
		ClassDB::bind_method(D_METHOD("read_vector"), &EGPBindingVictim::read_vector);
		ClassDB::bind_method(D_METHOD("read_array"), &EGPBindingVictim::read_array);
		ClassDB::bind_method(D_METHOD("read_object"), &EGPBindingVictim::read_object);
		ClassDB::bind_method(D_METHOD("read_bytes"), &EGPBindingVictim::read_bytes);
		ClassDB::bind_method(D_METHOD("read_variant"), &EGPBindingVictim::read_variant);
		ClassDB::bind_method(D_METHOD("read_bool"), &EGPBindingVictim::read_bool);
		ClassDB::bind_method(D_METHOD("read_float"), &EGPBindingVictim::read_float);
		ClassDB::bind_method(D_METHOD("set_counter", "value"), &EGPBindingVictim::set_counter);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "counter"), "set_counter", "get_counter");
	}

public:
	int64_t get_counter() const { return counter; }
	void set_counter(int64_t value) { counter = value; }
	String read_text() const { return "live"; }
	Vector3 read_vector() const { return Vector3(1, 2, 3); }
	Array read_array() const {
		Array value;
		value.append(91);
		return value;
	}
	Object *read_object() const { return const_cast<EGPBindingVictim *>(this); }
	PackedByteArray read_bytes() const {
		PackedByteArray value;
		value.append(91);
		return value;
	}
	Variant read_variant() const { return "live"; }
	bool read_bool() const { return true; }
	double read_float() const { return 91.5; }
#if BINDING_VERSION == 4
	int64_t read_value(int64_t offset) const { return counter + BINDING_VERSION * 100 + offset; }
	static int64_t read_static(int64_t offset) { return BINDING_VERSION * 100 + offset; }
#else
	int64_t read_value() const { return counter + BINDING_VERSION * 100; }
	static int64_t read_static() { return BINDING_VERSION * 100; }
#endif
};
class EGPBindingSpare : public Node {
	GDCLASS(EGPBindingSpare, Node);

protected:
	static void _bind_methods() {}
};
static void initialize(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(EGPBindingSpare);
#if BINDING_VERSION != 5
	GDREGISTER_CLASS(EGPBindingVictim);
#endif
}
static void terminate(ModuleInitializationLevel) {}
extern "C" GDExtensionBool GDE_EXPORT fixture_init(GDExtensionInterfaceGetProcAddress get_proc,
		GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization) {
	GDExtensionBinding::InitObject init(get_proc, library, initialization);
	init.register_initializer(initialize);
	init.register_terminator(terminate);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
