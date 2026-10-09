#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
using namespace godot;

// This library never reloads. It deliberately retains raw bindings belonging
// to a different library, independently of that library's instance bindings.
class EGPBindingObserver : public Node {
	GDCLASS(EGPBindingObserver, Node);
	GDExtensionMethodBindPtr instance_bind = nullptr, static_bind = nullptr;
	GDExtensionMethodBindPtr extra[8]{};

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("cache"), &EGPBindingObserver::cache);
		ClassDB::bind_method(D_METHOD("probe", "target", "ptrcall", "static_call"), &EGPBindingObserver::probe);
		ClassDB::bind_method(D_METHOD("probe_extra", "target", "ptrcall"), &EGPBindingObserver::probe_extra);
	}

public:
	bool cache() {
		StringName cls("EGPBindingVictim"), method("read_value"), static_method("read_static");
		instance_bind = gdextension_interface::classdb_get_method_bind(cls._native_ptr(), method._native_ptr(), 3905245786);
		static_bind = gdextension_interface::classdb_get_method_bind(cls._native_ptr(), static_method._native_ptr(), 2455072627);
		const char *names[] = { "read_text", "read_vector", "read_array", "read_object", "read_bytes", "read_variant", "read_bool", "read_float" };
		const int64_t hashes[] = { 201670096, 3360562783, 3995934104, 1981248198, 2362200018, 1214101251, 36873697, 1740695150 };
		for (int i = 0; i < 8; ++i) {
			StringName name(names[i]);
			extra[i] = gdextension_interface::classdb_get_method_bind(cls._native_ptr(), name._native_ptr(), hashes[i]);
			if (!extra[i]) {
				return false;
			}
		}
		return instance_bind && static_bind;
	}
	Array probe_extra(Object *target, bool ptrcall) {
		Array values;
		if (!ptrcall) {
			for (auto binding : extra) {
				Variant value;
				GDExtensionCallError error{};
				gdextension_interface::object_method_bind_call(binding, target->_owner, nullptr, 0, value._native_ptr(), &error);
				values.append(value);
				values.append(int64_t(error.error));
			}
			return values;
		}
		// Nonempty initialized return storage must be assigned, never leaked or
		// overwritten by placement construction. Scalars use the builtin ABI.
		String text("sentinel");
		Vector3 vector(9, 9, 9);
		Array array;
		array.append("sentinel");
		GDExtensionObjectPtr object = target->_owner;
		PackedByteArray bytes;
		bytes.append(77);
		Variant variant("sentinel");
		GDExtensionBool boolean = true;
		double number = -77;
		void *storage[] = { text._native_ptr(), &vector, array._native_ptr(), &object, bytes._native_ptr(), variant._native_ptr(), &boolean, &number };
		for (int i = 0; i < 8; ++i) {
			gdextension_interface::object_method_bind_ptrcall(extra[i], target->_owner, nullptr, storage[i]);
		}
		values.append(text);
		values.append(vector);
		values.append(array);
		values.append(object == target->_owner);
		values.append(object == nullptr);
		values.append(bytes);
		values.append(variant);
		values.append(bool(boolean));
		values.append(number);
		return values;
	}
	Dictionary probe(Object *target, bool ptrcall, bool static_call) {
		Dictionary result;
		const auto binding = static_call ? static_bind : instance_bind;
		const auto instance = static_call ? nullptr : target->_owner;
		if (ptrcall) {
			int64_t value = -77;
			gdextension_interface::object_method_bind_ptrcall(binding, instance, nullptr, &value);
			result["value"] = value;
		} else {
			Variant value;
			GDExtensionCallError error{};
			gdextension_interface::object_method_bind_call(binding, instance, nullptr, 0, value._native_ptr(), &error);
			result["value"] = value;
			result["error"] = int64_t(error.error);
			result["type"] = int64_t(value.get_type());
		}
		return result;
	}
};
static void initialize(ModuleInitializationLevel level) {
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EGPBindingObserver);
	}
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
