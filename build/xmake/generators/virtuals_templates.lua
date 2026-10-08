-- Engine virtual binding macro templates; native Lua generates each arity/qualifier.
function script_call() return [==[ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();\
		if (_script_instance) {\
			Callable::CallError ce;\
			$CALLSIARGS\
			$CALLSIBEGIN_script_instance->callp(_gdvirtual_##$VARNAME##_sn, $CALLSIARGPASS, ce);\
			if (ce.error == Callable::CallError::CALL_OK) {\
				$CALLSIRET\
				return true;\
			}\
		}]==] end
function script_has_method() return [==[ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();\
		if (_script_instance && _script_instance->has_method(_gdvirtual_##$VARNAME##_sn)) {\
			return true;\
		}]==] end
function proto() return [==[#define GDVIRTUAL$VER($ALIAS $RET m_name $ARG)\
	mutable void *_gdvirtual_##$VARNAME = nullptr;\
	_FORCE_INLINE_ bool _gdvirtual_##$VARNAME##_call($CALLARGS) $CONST {\
		static const StringName _gdvirtual_##$VARNAME##_sn = StringName(#m_name, true);\
		$SCRIPTCALL\
		if (_get_extension()) {\
			if (unlikely(!_gdvirtual_##$VARNAME)) {\
			    _gdvirtual_init_method_ptr(_gdvirtual_##$VARNAME##_get_method_info().get_compatibility_hash(), _gdvirtual_##$VARNAME, _gdvirtual_##$VARNAME##_sn, $COMPAT);\
			}\
			if (_gdvirtual_##$VARNAME != reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR)) {\
				$CALLPTRARGS\
				$CALLPTRRETDEF\
				if (_get_extension()->call_virtual_with_data) {\
					_get_extension()->call_virtual_with_data(_get_extension_instance(), &_gdvirtual_##$VARNAME##_sn, _gdvirtual_##$VARNAME, $CALLPTRARGPASS, $CALLPTRRETPASS);\
					$CALLPTRRET\
				} else {\
					((GDExtensionClassCallVirtual)_gdvirtual_##$VARNAME)(_get_extension_instance(), $CALLPTRARGPASS, $CALLPTRRETPASS);\
					$CALLPTRRET\
				}\
				return true;\
			}\
		}\
		$REQCHECK\
		$RVOID\
		return false;\
	}\
	_FORCE_INLINE_ bool _gdvirtual_##$VARNAME##_overridden() const {\
		static const StringName _gdvirtual_##$VARNAME##_sn = StringName(#m_name, true);\
		$SCRIPTHASMETHOD\
		if (_get_extension()) {\
			if (unlikely(!_gdvirtual_##$VARNAME)) {\
			    _gdvirtual_init_method_ptr(_gdvirtual_##$VARNAME##_get_method_info().get_compatibility_hash(), _gdvirtual_##$VARNAME, _gdvirtual_##$VARNAME##_sn, $COMPAT);\
			}\
			if (_gdvirtual_##$VARNAME != reinterpret_cast<void*>(_INVALID_GDVIRTUAL_FUNC_ADDR)) {\
				return true;\
			}\
		}\
		return false;\
	}\
	_FORCE_INLINE_ static MethodInfo _gdvirtual_##$VARNAME##_get_method_info() {\
		MethodInfo method_info;\
		method_info.name = #m_name;\
		method_info.flags = $METHOD_FLAGS;\
		$FILL_METHOD_INFO\
		return method_info;\
	}

]==] end
function header() return [==[/* THIS FILE IS GENERATED DO NOT EDIT */
#pragma once

// IWYU pragma: begin_keep
#include "core/object/script_instance.h"
#include "core/variant/method_ptrcall.h"
#include "core/variant/variant_caster.h"
#include "core/variant/variant_internal.h"
// IWYU pragma: end_keep

inline constexpr uintptr_t _INVALID_GDVIRTUAL_FUNC_ADDR = static_cast<uintptr_t>(-1);

template <typename... Args>
_NO_INLINE_ void _gdvirtual_set_method_info_args(MethodInfo &p_method_info) {
	p_method_info.arguments = { GetTypeInfo<Args>::get_class_info()... };
	p_method_info.arguments_metadata = { GetTypeInfo<Args>::METADATA... };
}

_NO_INLINE_ inline void _gdvirtual_print_required_error(const Object *p_object, const StringName &p_method_name, bool &r_error_shown) {
	// Like ERR_PRINT_ONCE but outlined, to save on binary space.
	if (!r_error_shown) {
		r_error_shown = true;
		ERR_PRINT(vformat("Required virtual method %s::%s must be overridden before calling.", p_object->get_class_name(), p_method_name));
	}
}

]==] end
