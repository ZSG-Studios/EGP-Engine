-- Native Lua SDK emitter, ported from the pinned godot-cpp algorithm. See its MIT LICENSE.md.
import("build.xmake.generators.sdk_runtime", {rootdir = os.projectdir()})
local rt = sdk_runtime.main()
local v_generate_mod_version,v_generate_wrappers,v_generate_virtual_version,v_generate_virtuals,v_get_file_list,v__get_file_list,v_print_file_list,v_generate_bindings,v__generate_bindings,v_generate_gdextension_interface_loader,v_gdextension_interface_type_name,v_generate_gdextension_interface_loader_header,v_generate_gdextension_interface_loader_source,v_generate_builtin_bindings,v_generate_builtin_class_vararg_method_implements_header,v_generate_builtin_class_header,v_generate_builtin_class_source,v_generate_engine_classes_bindings,v_generate_engine_class_header,v_generate_engine_class_source,v_generate_global_constants,v_generate_version_header,v_generate_global_constant_binds,v_generate_utility_functions,v_camel_to_snake,v_make_function_parameters,v_type_for_parameter,v_get_include_path,v_get_encoded_arg,v_make_signature,v_make_varargs_template,v_is_pod_type,v_is_included_type,v_is_included_struct_type,v_is_packed_array,v_needs_copy_instead_of_move,v_is_enum,v_is_bitfield,v_get_enum_class,v_get_enum_fullname,v_get_enum_name,v_is_variant,v_is_engine_class,v_is_struct_type,v_is_refcounted,v_is_included,v_correct_default_value,v_correct_typed_array,v_correct_typed_dictionary,v_correct_type,v_get_gdextension_type,v_escape_identifier,v_escape_argument,v_get_operator_id_name,v_get_operator_cpp_name,v_is_valid_cpp_operator,v_get_default_value_for_type,v_add_header,v_CLASS_ALIASES,v_builtin_classes,v_engine_classes,v_native_structures,v_singletons,v_header
local v_len,v_str,v_print,v_open,v_range,v_enumerate,v_sorted,v_set,v_list,v_map,v_Exception,v_Path,v_isinstance = rt.len,rt.str,print,rt.open,rt.range,rt.enumerate,rt.sorted,rt.set,rt.list,rt.map,rt.str,rt.Path,rt.isinstance
local v_json,v_shutil,v_re,v_OrderedDict,v_UnknownTypeError = rt.json,rt.shutil,rt.re,rt.dict,rt.unknown_type
local v_generate_gdextension_interface_header = rt.interface_header
v_generate_mod_version =
rt.fn({"argcount","const","returns"},{rt._missing,false,false},function(v_argcount,v_const,v_returns)
    local v_argtext,v_funcargs,v_i,v_s,v_sproto
    v_s = "\n#define MODBIND$VER($RETTYPE m_name$ARG) \\\nvirtual $RETVAL _##m_name($FUNCARGS) $CONST override;\n"
    v_sproto = rt.call(v_str,rt.arguments({v_argcount}),rt.dict({}))
    if rt.truth(v_returns) then
        v_sproto = rt.add(v_sproto,"R")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RETTYPE","m_ret, "}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RETVAL","m_ret"}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RETTYPE",""}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RETVAL","void"}),rt.dict({}))
    end
    if rt.truth(v_const) then
        v_sproto = rt.add(v_sproto,"C")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CONST","const"}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CONST",""}),rt.dict({}))
    end
    v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$VER",v_sproto}),rt.dict({}))
    v_argtext = ""
    v_funcargs = ""
    for temporary_1 in rt.iter(rt.call(v_range,rt.arguments({v_argcount}),rt.dict({}))) do
        do
            v_i = temporary_1
            if rt.truth((((v_i>0)))) then
                v_funcargs = rt.add(v_funcargs,", ")
            end
            v_argtext = rt.add(v_argtext,rt.add(", m_type",rt.call(v_str,rt.arguments({rt.add(v_i,1)}),rt.dict({}))))
            v_funcargs = rt.add(v_funcargs,rt.add(rt.add(rt.add("m_type",rt.call(v_str,rt.arguments({rt.add(v_i,1)}),rt.dict({})))," arg"),rt.call(v_str,rt.arguments({rt.add(v_i,1)}),rt.dict({}))))
        end
        ::temporary_2::
    end
    if rt.truth(v_argcount) then
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$ARG",v_argtext}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$FUNCARGS",v_funcargs}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$ARG",""}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$FUNCARGS",v_funcargs}),rt.dict({}))
    end
    do return v_s end
end)
v_generate_wrappers =
rt.fn({"target"},{rt._missing},function(v_target)
    local v_f,v_i,v_max_versions,v_txt
    v_max_versions = 12
    v_txt = "#pragma once"
    for temporary_3 in rt.iter(rt.call(v_range,rt.arguments({rt.add(v_max_versions,1)}),rt.dict({}))) do
        do
            v_i = temporary_3
            v_txt = rt.add(v_txt,rt.add(rt.add("\n/* Module Wrapper ",rt.call(v_str,rt.arguments({v_i}),rt.dict({})))," Arguments */\n"))
            v_txt = rt.add(v_txt,rt.call(v_generate_mod_version,rt.arguments({v_i,false,false}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_mod_version,rt.arguments({v_i,false,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_mod_version,rt.arguments({v_i,true,false}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_mod_version,rt.arguments({v_i,true,true}),rt.dict({})))
        end
        ::temporary_4::
    end
    do
        local v_f = rt.call(v_open,rt.arguments({v_target,"w"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_f,"write"),rt.arguments({v_txt}),rt.dict({}))
        rt.close(v_f)
    end
end)
v_generate_virtual_version =
rt.fn({"argcount","const","returns","required"},{rt._missing,false,false,false},function(v_argcount,v_const,v_returns,v_required)
    local v_argtext,v_callargtext,v_callsiargptrs,v_callsiargs,v_i,v_method_flags,v_method_info,v_s,v_sproto
    v_s = "#define GDVIRTUAL$VER($RET m_name $ARG)\\\n\t::godot::StringName _gdvirtual_##m_name##_sn = #m_name;\\\n\t_FORCE_INLINE_ bool _gdvirtual_##m_name##_call($CALLARGS) $CONST {\\\n\t\tif (::godot::gdextension_interface::object_has_script_method(_owner, &_gdvirtual_##m_name##_sn)) { \\\n\t\t\tGDExtensionCallError ce;\\\n\t\t\t$CALLSIARGS\\\n\t\t\t::godot::Variant ret;\\\n\t\t\t::godot::gdextension_interface::object_call_script_method(_owner, &_gdvirtual_##m_name##_sn, $CALLSIARGPASS, &ret, &ce);\\\n\t\t\tif (ce.error == GDEXTENSION_CALL_OK) {\\\n\t\t\t\t$CALLSIRET\\\n\t\t\t\treturn true;\\\n\t\t\t}\\\n\t\t}\\\n\t\t$REQCHECK\\\n\t\t$RVOID\\\n\t\treturn false;\\\n\t}\\\n\t_FORCE_INLINE_ bool _gdvirtual_##m_name##_overridden() const {\\\n\t\treturn ::godot::gdextension_interface::object_has_script_method(_owner, &_gdvirtual_##m_name##_sn); \\\n\t}\\\n\t_FORCE_INLINE_ static ::godot::MethodInfo _gdvirtual_##m_name##_get_method_info() {\\\n\t\t::godot::MethodInfo method_info;\\\n\t\tmethod_info.name = #m_name;\\\n\t\tmethod_info.flags = $METHOD_FLAGS;\\\n\t\t$FILL_METHOD_INFO\\\n\t\treturn method_info;\\\n\t}\n\n"
    v_sproto = rt.call(v_str,rt.arguments({v_argcount}),rt.dict({}))
    v_method_info = ""
    v_method_flags = "::godot::MethodFlags::METHOD_FLAG_VIRTUAL"
    if rt.truth(v_returns) then
        v_sproto = rt.add(v_sproto,"R")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RET","m_ret,"}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RVOID","(void)r_ret;"}),rt.dict({}))
        v_method_info = rt.add(v_method_info,"method_info.return_val = ::godot::GetTypeInfo<m_ret>::get_class_info();\\\n")
        v_method_info = rt.add(v_method_info,"\t\tmethod_info.return_val_metadata = ::godot::GetTypeInfo<m_ret>::METADATA;")
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$RET ",""}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"\t\t$RVOID\\\n",""}),rt.dict({}))
    end
    if rt.truth(v_const) then
        v_sproto = rt.add(v_sproto,"C")
        v_method_flags = rt.add(v_method_flags," | ::godot::MethodFlags::METHOD_FLAG_CONST")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CONST","const"}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CONST ",""}),rt.dict({}))
    end
    if rt.truth(v_required) then
        v_sproto = rt.add(v_sproto,"_REQUIRED")
        v_method_flags = rt.add(v_method_flags," | ::godot::MethodFlags::METHOD_FLAG_VIRTUAL_REQUIRED")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$REQCHECK","ERR_PRINT_ONCE(\"Required virtual method \" + get_class() + \"::\" + #m_name + \" must be overridden before calling.\");"}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"\t\t$REQCHECK\\\n",""}),rt.dict({}))
    end
    v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$METHOD_FLAGS",v_method_flags}),rt.dict({}))
    v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$VER",v_sproto}),rt.dict({}))
    v_argtext = ""
    v_callargtext = ""
    v_callsiargs = ""
    v_callsiargptrs = ""
    if rt.truth((((v_argcount>0)))) then
        v_argtext = rt.add(v_argtext,", ")
        v_callsiargs = ("::godot::Variant vargs[" .. rt.str(v_argcount) .. "] = { ")
        v_callsiargptrs = ("\t\t\tconst ::godot::Variant *vargptrs[" .. rt.str(v_argcount) .. "] = { ")
    end
    for temporary_5 in rt.iter(rt.call(v_range,rt.arguments({v_argcount}),rt.dict({}))) do
        do
            v_i = temporary_5
            if rt.truth((((v_i>0)))) then
                v_argtext = rt.add(v_argtext,", ")
                v_callargtext = rt.add(v_callargtext,", ")
                v_callsiargs = rt.add(v_callsiargs,", ")
                v_callsiargptrs = rt.add(v_callsiargptrs,", ")
            end
            v_argtext = rt.add(v_argtext,("m_type" .. rt.str(rt.add(v_i,1))))
            v_callargtext = rt.add(v_callargtext,("m_type" .. rt.str(rt.add(v_i,1)) .. " arg" .. rt.str(rt.add(v_i,1))))
            v_callsiargs = rt.add(v_callsiargs,("::godot::Variant(arg" .. rt.str(rt.add(v_i,1)) .. ")"))
            v_callsiargptrs = rt.add(v_callsiargptrs,("&vargs[" .. rt.str(v_i) .. "]"))
            if rt.truth(v_method_info) then
                v_method_info = rt.add(v_method_info,"\\\n\t\t")
            end
            v_method_info = rt.add(v_method_info,("method_info.arguments.push_back(::godot::GetTypeInfo<m_type" .. rt.str(rt.add(v_i,1)) .. ">::get_class_info());\\\n"))
            v_method_info = rt.add(v_method_info,("\t\tmethod_info.arguments_metadata.push_back(::godot::GetTypeInfo<m_type" .. rt.str(rt.add(v_i,1)) .. ">::METADATA);"))
        end
        ::temporary_6::
    end
    if rt.truth(v_argcount) then
        v_callsiargs = rt.add(v_callsiargs," };\\\n")
        v_callsiargptrs = rt.add(v_callsiargptrs," };")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CALLSIARGS",rt.add(v_callsiargs,v_callsiargptrs)}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CALLSIARGPASS",("(const GDExtensionConstVariantPtr *)vargptrs, " .. rt.str(v_argcount))}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"\t\t\t$CALLSIARGS\\\n",""}),rt.dict({}))
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CALLSIARGPASS","nullptr, 0"}),rt.dict({}))
    end
    if rt.truth(v_returns) then
        if rt.truth((((v_argcount>0)))) then
            v_callargtext = rt.add(v_callargtext,", ")
        end
        v_callargtext = rt.add(v_callargtext,"m_ret &r_ret")
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CALLSIRET","r_ret = ::godot::VariantCaster<m_ret>::cast(ret);"}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"\t\t\t\t$CALLSIRET\\\n",""}),rt.dict({}))
    end
    v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({" $ARG",v_argtext}),rt.dict({}))
    v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$CALLARGS",v_callargtext}),rt.dict({}))
    if rt.truth(v_method_info) then
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"$FILL_METHOD_INFO",v_method_info}),rt.dict({}))
    else
        v_s = rt.call(rt.attr(v_s,"replace"),rt.arguments({"\t\t$FILL_METHOD_INFO\\\n",v_method_info}),rt.dict({}))
    end
    do return v_s end
end)
v_generate_virtuals =
rt.fn({"target"},{rt._missing},function(v_target)
    local v_f,v_i,v_max_versions,v_txt
    v_max_versions = 12
    v_txt = "/* THIS FILE IS GENERATED DO NOT EDIT */\n#pragma once\n\n"
    for temporary_7 in rt.iter(rt.call(v_range,rt.arguments({rt.add(v_max_versions,1)}),rt.dict({}))) do
        do
            v_i = temporary_7
            v_txt = rt.add(v_txt,("/* " .. rt.str(v_i) .. " Arguments */\n\n"))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,false,false}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,false,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,true,false}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,true,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,false,false,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,false,true,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,true,false,true}),rt.dict({})))
            v_txt = rt.add(v_txt,rt.call(v_generate_virtual_version,rt.arguments({v_i,true,true,true}),rt.dict({})))
        end
        ::temporary_8::
    end
    do
        local v_f = rt.call(v_open,rt.arguments({v_target,"w"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_f,"write"),rt.arguments({v_txt}),rt.dict({}))
        rt.close(v_f)
    end
end)
v_get_file_list =
rt.fn({"api_filepath","output_dir","headers","sources"},{rt._missing,rt._missing,false,false},function(v_api_filepath,v_output_dir,v_headers,v_sources)
    local v_api,v_api_file
    v_api = rt.dict({})
    do
        local v_api_file = rt.call(v_open,rt.arguments({v_api_filepath}),rt.dict({{"encoding","utf-8"}}))
        v_api = rt.call(rt.attr(v_json,"load"),rt.arguments({v_api_file}),rt.dict({}))
        rt.close(v_api_file)
    end
    do return rt.call(v__get_file_list,rt.arguments({v_api,v_output_dir,v_headers,v_sources}),rt.dict({})) end
end)
v__get_file_list =
rt.fn({"api","output_dir","headers","sources"},{rt._missing,rt._missing,false,false},function(v_api,v_output_dir,v_headers,v_sources)
    local v_builtin_class,v_core_gen_folder,v_engine_class,v_files,v_gdextension_gen_folder,v_header_filename,v_include_gen_folder,v_native_struct,v_path,v_snake_struct_name,v_source_filename,v_source_gen_folder,v_struct_name,v_utility_functions_source_path
    v_files = rt.array({})
    v_gdextension_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen"),"include")
    v_core_gen_folder = rt.div(rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen"),"include"),"godot_cpp"),"core")
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen"),"include"),"godot_cpp")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen"),"src")
    rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_gdextension_gen_folder,"gdextension_interface.h"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_core_gen_folder,"gdextension_interface_loader.hpp"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_source_gen_folder,"gdextension_interface_loader.cpp"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_core_gen_folder,"ext_wrappers.gen.inc"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_core_gen_folder,"gdvirtual.gen.inc"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    for temporary_9 in rt.iter(rt.get(v_api,"builtin_classes")) do
        do
            v_builtin_class = temporary_9
            if rt.truth(rt.call(v_is_pod_type,rt.arguments({rt.get(v_builtin_class,"name")}),rt.dict({}))) then
                goto temporary_10
            end
            if rt.truth(rt.call(v_is_included_type,rt.arguments({rt.get(v_builtin_class,"name")}),rt.dict({}))) then
                goto temporary_10
            end
            v_header_filename = rt.div(rt.div(v_include_gen_folder,"variant"),rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_builtin_class,"name")}),rt.dict({})),".hpp"))
            v_source_filename = rt.div(rt.div(v_source_gen_folder,"variant"),rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_builtin_class,"name")}),rt.dict({})),".cpp"))
            if rt.truth(v_headers) then
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_header_filename,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
            if rt.truth(v_sources) then
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_source_filename,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
        end
        ::temporary_10::
    end
    for temporary_11 in rt.iter(rt.get(v_api,"classes")) do
        do
            v_engine_class = temporary_11
            if rt.truth((((rt.get(v_engine_class,"name")=="ClassDB")))) then
                rt.put(v_engine_class,"name","ClassDBSingleton")
                rt.put(v_engine_class,"alias_for","ClassDB")
            end
            v_header_filename = rt.div(rt.div(v_include_gen_folder,"classes"),rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_engine_class,"name")}),rt.dict({})),".hpp"))
            v_source_filename = rt.div(rt.div(v_source_gen_folder,"classes"),rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_engine_class,"name")}),rt.dict({})),".cpp"))
            if rt.truth(v_headers) then
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_header_filename,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
            if rt.truth(v_sources) then
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_source_filename,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
        end
        ::temporary_12::
    end
    for temporary_13 in rt.iter(rt.get(v_api,"native_structures")) do
        do
            v_native_struct = temporary_13
            v_struct_name = rt.get(v_native_struct,"name")
            if rt.truth((((v_struct_name=="ObjectID")))) then
                goto temporary_14
            end
            v_snake_struct_name = rt.call(v_camel_to_snake,rt.arguments({v_struct_name}),rt.dict({}))
            v_header_filename = rt.div(rt.div(v_include_gen_folder,"classes"),rt.add(v_snake_struct_name,".hpp"))
            if rt.truth(v_headers) then
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_header_filename,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
        end
        ::temporary_14::
    end
    if rt.truth(v_headers) then
        for temporary_15 in rt.iter(rt.array({rt.div(rt.div(v_include_gen_folder,"variant"),"builtin_types.hpp"),rt.div(rt.div(v_include_gen_folder,"variant"),"builtin_binds.hpp"),rt.div(rt.div(v_include_gen_folder,"variant"),"utility_functions.hpp"),rt.div(rt.div(v_include_gen_folder,"variant"),"variant_size.hpp"),rt.div(rt.div(v_include_gen_folder,"variant"),"builtin_vararg_methods.hpp"),rt.div(rt.div(v_include_gen_folder,"classes"),"global_constants.hpp"),rt.div(rt.div(v_include_gen_folder,"classes"),"global_constants_binds.hpp"),rt.div(rt.div(v_include_gen_folder,"core"),"version.hpp")})) do
            do
                v_path = temporary_15
                rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_path,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
            end
            ::temporary_16::
        end
    end
    if rt.truth(v_sources) then
        v_utility_functions_source_path = rt.div(rt.div(v_source_gen_folder,"variant"),"utility_functions.cpp")
        rt.call(rt.attr(v_files,"append"),rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(v_utility_functions_source_path,"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
    end
    do return v_files end
end)
v_print_file_list =
rt.fn({"api_filepath","output_dir","headers","sources"},{rt._missing,rt._missing,false,false},function(v_api_filepath,v_output_dir,v_headers,v_sources)
    rt.call(v_print,rt.arguments({rt.spread(rt.call(v_get_file_list,rt.arguments({v_api_filepath,v_output_dir,v_headers,v_sources}),rt.dict({})))}),rt.dict({{"sep",";"},{"end",rt._none}}))
end)
v_generate_bindings =
rt.fn({"api_filepath","interface_filepath","use_template_get_node","bits","precision","output_dir","hooks_path"},{rt._missing,rt._missing,rt._missing,"64","single",".",rt._none},function(v_api_filepath,v_interface_filepath,v_use_template_get_node,v_bits,v_precision,v_output_dir,v_hooks_path)
    local v_api,v_api_file,v_custom_hooks,v_loaded_module,v_spec
    v_api = rt.dict({})
    do
        local v_api_file = rt.call(v_open,rt.arguments({v_api_filepath}),rt.dict({{"encoding","utf-8"}}))
        v_api = rt.call(rt.attr(v_json,"load"),rt.arguments({v_api_file}),rt.dict({}))
        rt.close(v_api_file)
    end
    v_custom_hooks = rt._none
    if rt.truth(v_hooks_path) then
        raise("External executable-language hooks are not part of the bundled SDK contract")
    end
    rt.call(v__generate_bindings,rt.arguments({v_api,v_api_filepath,v_interface_filepath,v_use_template_get_node,v_bits,v_precision,v_output_dir,v_custom_hooks}),rt.dict({}))
end)
v__generate_bindings =
rt.fn({"api","api_filepath","interface_filepath","use_template_get_node","bits","precision","output_dir","hooks"},{rt._missing,rt._missing,rt._missing,rt._missing,"64","single",".",rt._none},function(v_api,v_api_filepath,v_interface_filepath,v_use_template_get_node,v_bits,v_precision,v_output_dir,v_hooks)
    local v_gdextension_gen_folder,v_header_lines,v_real_t,v_target_dir
    if rt.truth(rt.logical(((rt.contains(rt.get(v_api,"header"),"precision"))),function() return (((v_precision~=rt.get(rt.get(v_api,"header"),"precision")))) end,true)) then
        raise(rt.str(rt.call(v_Exception,rt.arguments({("Cannot do a precision=" .. rt.str(v_precision) .. " build using '" .. rt.str(v_api_filepath) .. "' which was generated by Godot built with precision=" .. rt.str(rt.get(rt.get(v_api,"header"),"precision")))}),rt.dict({}))))
    end
    v_target_dir = rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen")
    rt.call(rt.attr(v_shutil,"rmtree"),rt.arguments({v_target_dir}),rt.dict({{"ignore_errors",true}}))
    rt.call(rt.attr(v_target_dir,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_real_t = rt.choose((((v_precision=="double"))),function() return "double" end,function() return "float" end)
    rt.call(v_print,rt.arguments({rt.add(rt.add(rt.add("Built-in type config: ",v_real_t),"_"),v_bits)}),rt.dict({}))
    v_header_lines = rt.array({})
    rt.call(v_add_header,rt.arguments({"gdextension_interface.h",v_header_lines}),rt.dict({}))
    v_gdextension_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"gen"),"include")
    rt.call(rt.attr(v_gdextension_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(v_generate_gdextension_interface_header,rt.arguments({rt.call(v_str,rt.arguments({rt.call(rt.attr(rt.div(v_gdextension_gen_folder,"gdextension_interface.h"),"as_posix"),rt.arguments({}),rt.dict({}))}),rt.dict({})),v_interface_filepath,v_header_lines}),rt.dict({}))
    rt.call(v_generate_gdextension_interface_loader,rt.arguments({v_interface_filepath,v_target_dir}),rt.dict({}))
    rt.call(v_generate_global_constants,rt.arguments({v_api,v_target_dir,v_hooks}),rt.dict({}))
    rt.call(v_generate_version_header,rt.arguments({v_api,v_target_dir}),rt.dict({}))
    rt.call(v_generate_global_constant_binds,rt.arguments({v_api,v_target_dir}),rt.dict({}))
    rt.call(v_generate_builtin_bindings,rt.arguments({v_api,v_target_dir,rt.add(rt.add(v_real_t,"_"),v_bits),v_hooks}),rt.dict({}))
    rt.call(v_generate_engine_classes_bindings,rt.arguments({v_api,v_target_dir,v_use_template_get_node,v_hooks}),rt.dict({}))
    rt.call(v_generate_utility_functions,rt.arguments({v_api,v_target_dir,v_hooks}),rt.dict({}))
end)
v_generate_gdextension_interface_loader =
rt.fn({"interface_filepath","output_dir"},{rt._missing,rt._missing},function(v_interface_filepath,v_output_dir)
    local v_data,v_file,v_func,v_functions_by_version,v_header_file,v_header_filename,v_include_gen_folder,v_since,v_source_file,v_source_filename,v_source_gen_folder
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"core")
    v_source_gen_folder = rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_header_filename = rt.div(v_include_gen_folder,"gdextension_interface_loader.hpp")
    v_source_filename = rt.div(v_source_gen_folder,"gdextension_interface_loader.cpp")
    do
        local v_file = rt.call(v_open,rt.arguments({v_interface_filepath,"rt"}),rt.dict({{"encoding","utf-8"}}))
        v_data = rt.call(rt.attr(v_json,"load"),rt.arguments({v_file}),rt.dict({}))
        rt.close(v_file)
    end
    v_functions_by_version = rt.dict({})
    for temporary_17 in rt.iter(rt.get(v_data,"interface")) do
        do
            v_func = temporary_17
            v_since = rt.get(v_func,"since")
            if rt.truth(((not rt.contains(v_functions_by_version,v_since)))) then
                rt.put(v_functions_by_version,v_since,rt.array({}))
            end
            rt.call(rt.attr(rt.get(v_functions_by_version,v_since),"append"),rt.arguments({v_func}),rt.dict({}))
        end
        ::temporary_18::
    end
    do
        local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"wt"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(v_generate_gdextension_interface_loader_header,rt.arguments({v_functions_by_version}),rt.dict({}))}),rt.dict({}))
        rt.close(v_header_file)
    end
    do
        local v_source_file = rt.call(rt.attr(v_source_filename,"open"),rt.arguments({"wt"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_source_file,"write"),rt.arguments({rt.call(v_generate_gdextension_interface_loader_source,rt.arguments({v_functions_by_version}),rt.dict({}))}),rt.dict({}))
        rt.close(v_source_file)
    end
end)
v_gdextension_interface_type_name =
rt.fn({"name"},{rt._missing},function(v_name)
    local v_word
    do return rt.add("GDExtensionInterface",rt.call(rt.attr("","join"),rt.arguments({(function() local temporary_19=rt.array({}); for temporary_20 in rt.iter(rt.call(rt.attr(v_name,"split"),rt.arguments({"_"}),rt.dict({}))) do v_word = temporary_20; if true then rt.append(temporary_19,rt.call(rt.attr(v_word,"capitalize"),rt.arguments({}),rt.dict({}))) end end; return temporary_19 end)()}),rt.dict({}))) end
end)
v_generate_gdextension_interface_loader_header =
rt.fn({"data"},{rt._missing},function(v_data)
    local v_deprecated_major,v_deprecated_minor,v_fn,v_func,v_major,v_minor,v_name,v_result,v_version,v_versions
    v_result = rt.array({})
    rt.call(v_add_header,rt.arguments({"gdextension_interface_loader.hpp",v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <gdextension_interface.h>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/version.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace gdextension_interface {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    v_versions = rt.call(v_sorted,rt.arguments({rt.call(rt.attr(v_data,"keys"),rt.arguments({}),rt.dict({}))}),rt.dict({}))
    for temporary_21 in rt.iter(v_versions) do
        do
            v_version = temporary_21
            v_major = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),0); v_minor = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),1)
            rt.call(rt.attr(v_result,"append"),rt.arguments({("// Godot 4." .. rt.str(v_minor) .. " or newer.")}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#if GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            for temporary_23 in rt.iter(rt.get(v_data,v_version)) do
                do
                    v_func = temporary_23
                    v_name = rt.get(v_func,"name")
                    v_fn = rt.call(v_gdextension_interface_type_name,rt.arguments({v_name}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        v_deprecated_major = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),0); v_deprecated_minor = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),1)
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("#if !defined(DISABLE_DEPRECATED) || GODOT_VERSION_MINOR < " .. rt.str(v_deprecated_minor))}),rt.dict({}))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("extern \"C\" " .. rt.str(v_fn) .. " " .. rt.str(v_name) .. ";")}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif"}),rt.dict({}))
                    end
                end
                ::temporary_24::
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#endif // GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        end
        ::temporary_22::
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace gdextension_interface"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace internal {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"bool load_gdextension_interface(GDExtensionInterfaceGetProcAddress p_get_proc_address);"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace internal"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_gdextension_interface_loader_source =
rt.fn({"data"},{rt._missing},function(v_data)
    local v_deprecated_major,v_deprecated_minor,v_fn,v_func,v_major,v_minor,v_name,v_result,v_version,v_versions
    v_result = rt.array({})
    rt.call(v_add_header,rt.arguments({"gdextension_interface_loader.cpp",v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/error_macros.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/gdextension_interface_loader.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/load_proc_address.inc>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace gdextension_interface {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    v_versions = rt.call(v_sorted,rt.arguments({rt.call(rt.attr(v_data,"keys"),rt.arguments({}),rt.dict({}))}),rt.dict({}))
    for temporary_25 in rt.iter(v_versions) do
        do
            v_version = temporary_25
            v_major = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),0); v_minor = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),1)
            rt.call(rt.attr(v_result,"append"),rt.arguments({("// Godot 4." .. rt.str(v_minor) .. " or newer.")}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#if GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            for temporary_27 in rt.iter(rt.get(v_data,v_version)) do
                do
                    v_func = temporary_27
                    v_name = rt.get(v_func,"name")
                    v_fn = rt.call(v_gdextension_interface_type_name,rt.arguments({v_name}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        v_deprecated_major = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),0); v_deprecated_minor = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),1)
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("#if !defined(DISABLE_DEPRECATED) || GODOT_VERSION_MINOR < " .. rt.str(v_deprecated_minor))}),rt.dict({}))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_fn) .. " " .. rt.str(v_name) .. " = nullptr;")}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif"}),rt.dict({}))
                    end
                end
                ::temporary_28::
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#endif // GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        end
        ::temporary_26::
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace gdextension_interface"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace internal {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"bool load_gdextension_interface(GDExtensionInterfaceGetProcAddress p_get_proc_address) {"}),rt.dict({}))
    for temporary_29 in rt.iter(v_versions) do
        do
            v_version = temporary_29
            v_major = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),0); v_minor = rt.get(rt.call(rt.attr(v_version,"split"),rt.arguments({"."}),rt.dict({})),1)
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\t// Godot 4." .. rt.str(v_minor) .. " or newer.")}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#if GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            for temporary_31 in rt.iter(rt.get(v_data,v_version)) do
                do
                    v_func = temporary_31
                    v_name = rt.get(v_func,"name")
                    if rt.truth((((v_name=="print_error")))) then
                        goto temporary_32
                    end
                    v_fn = rt.call(v_gdextension_interface_type_name,rt.arguments({v_name}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        v_deprecated_major = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),0); v_deprecated_minor = rt.get(rt.call(rt.attr(rt.get(rt.get(v_func,"deprecated"),"since"),"split"),rt.arguments({"."}),rt.dict({})),1)
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("#if !defined(DISABLE_DEPRECATED) || GODOT_VERSION_MINOR < " .. rt.str(v_deprecated_minor))}),rt.dict({}))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tLOAD_PROC_ADDRESS(" .. rt.str(v_name) .. ", " .. rt.str(v_fn) .. ");")}),rt.dict({}))
                    if rt.truth(((rt.contains(v_func,"deprecated")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif"}),rt.dict({}))
                    end
                end
                ::temporary_32::
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({("#endif // GODOT_VERSION_MINOR >= " .. rt.str(v_minor))}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        end
        ::temporary_30::
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn true;"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace internal"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_CLASS_ALIASES = rt.dict({{"ClassDB","ClassDBSingleton"}})
v_builtin_classes = rt.array({})
v_engine_classes = rt.dict({})
v_native_structures = rt.array({})
v_singletons = rt.array({})
v_generate_builtin_bindings =
rt.fn({"api","output_dir","build_config","hooks"},{rt._missing,rt._missing,rt._missing,rt._none},function(v_api,v_output_dir,v_build_config,v_hooks)
    local v_argument,v_builtin,v_builtin_api,v_builtin_binds,v_builtin_binds_file,v_builtin_binds_filename,v_builtin_header,v_builtin_header_file,v_builtin_header_filename,v_builtin_sizes,v_builtin_vararg_methods_header,v_class_name,v_constructor,v_core_gen_folder,v_enum_api,v_fully_used_classes,v_header_file,v_header_filename,v_include,v_include_gen_folder,v_includes,v_member,v_method,v_operator,v_size,v_size_list,v_source_file,v_source_filename,v_source_gen_folder,v_type_name,v_used_classes,v_vararg_methods_file,v_variant_size_file,v_variant_size_filename,v_variant_size_source
    v_core_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"core")
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"variant")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src"),"variant")
    rt.call(rt.attr(v_core_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(v_generate_wrappers,rt.arguments({rt.div(v_core_gen_folder,"ext_wrappers.gen.inc")}),rt.dict({}))
    rt.call(v_generate_virtuals,rt.arguments({rt.div(v_core_gen_folder,"gdvirtual.gen.inc")}),rt.dict({}))
    for temporary_33 in rt.iter(rt.get(v_api,"builtin_classes")) do
        do
            v_builtin_api = temporary_33
            if rt.truth(rt.call(v_is_pod_type,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({}))) then
                goto temporary_34
            end
            rt.call(rt.attr(v_builtin_classes,"append"),rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({}))
        end
        ::temporary_34::
    end
    v_builtin_sizes = rt.dict({})
    for temporary_35 in rt.iter(rt.get(v_api,"builtin_class_sizes")) do
        do
            v_size_list = temporary_35
            if rt.truth((((rt.get(v_size_list,"build_configuration")==v_build_config)))) then
                for temporary_37 in rt.iter(rt.get(v_size_list,"sizes")) do
                    do
                        v_size = temporary_37
                        rt.put(v_builtin_sizes,rt.get(v_size,"name"),rt.get(v_size,"size"))
                    end
                    ::temporary_38::
                end
                break
            end
        end
        ::temporary_36::
    end
    v_variant_size_filename = rt.div(v_include_gen_folder,"variant_size.hpp")
    do
        local v_variant_size_file = rt.call(rt.attr(v_variant_size_filename,"open"),rt.arguments({"+w"}),rt.dict({{"encoding","utf-8"}}))
        v_variant_size_source = rt.array({})
        rt.call(v_add_header,rt.arguments({"variant_size.hpp",v_variant_size_source}),rt.dict({}))
        rt.call(rt.attr(v_variant_size_source,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
        rt.call(rt.attr(v_variant_size_source,"append"),rt.arguments({("#define GODOT_CPP_VARIANT_SIZE " .. rt.str(rt.get(v_builtin_sizes,"Variant")))}),rt.dict({}))
        rt.call(rt.attr(v_variant_size_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_variant_size_source}),rt.dict({}))}),rt.dict({}))
        rt.close(v_variant_size_file)
    end
    for temporary_39 in rt.iter(rt.get(v_api,"builtin_classes")) do
        do
            v_builtin_api = temporary_39
            if rt.truth(rt.call(v_is_pod_type,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({}))) then
                goto temporary_40
            end
            if rt.truth(rt.call(v_is_included_type,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({}))) then
                goto temporary_40
            end
            v_size = rt.get(v_builtin_sizes,rt.get(v_builtin_api,"name"))
            v_header_filename = rt.div(v_include_gen_folder,rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({})),".hpp"))
            v_source_filename = rt.div(v_source_gen_folder,rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({})),".cpp"))
            v_used_classes = rt.call(v_set,rt.arguments({}),rt.dict({}))
            v_fully_used_classes = rt.call(v_set,rt.arguments({}),rt.dict({}))
            v_class_name = rt.get(v_builtin_api,"name")
            if rt.truth(((rt.contains(v_builtin_api,"constructors")))) then
                for temporary_41 in rt.iter(rt.get(v_builtin_api,"constructors")) do
                    do
                        v_constructor = temporary_41
                        if rt.truth(((rt.contains(v_constructor,"arguments")))) then
                            for temporary_43 in rt.iter(rt.get(v_constructor,"arguments")) do
                                do
                                    v_argument = temporary_43
                                    if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_argument,"type"),v_class_name}),rt.dict({}))) then
                                        if rt.truth(rt.logical(((rt.contains(v_argument,"default_value"))),function() return (((rt.get(v_argument,"type")~="Variant"))) end,true)) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.get(v_argument,"type")}),rt.dict({}))
                                        else
                                            rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_argument,"type")}),rt.dict({}))
                                        end
                                    end
                                end
                                ::temporary_44::
                            end
                        end
                    end
                    ::temporary_42::
                end
            end
            if rt.truth(((rt.contains(v_builtin_api,"methods")))) then
                for temporary_45 in rt.iter(rt.get(v_builtin_api,"methods")) do
                    do
                        v_method = temporary_45
                        if rt.truth(((rt.contains(v_method,"arguments")))) then
                            for temporary_47 in rt.iter(rt.get(v_method,"arguments")) do
                                do
                                    v_argument = temporary_47
                                    if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_argument,"type"),v_class_name}),rt.dict({}))) then
                                        if rt.truth(rt.logical(((rt.contains(v_argument,"default_value"))),function() return (((rt.get(v_argument,"type")~="Variant"))) end,true)) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.get(v_argument,"type")}),rt.dict({}))
                                        else
                                            rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_argument,"type")}),rt.dict({}))
                                        end
                                    end
                                end
                                ::temporary_48::
                            end
                        end
                        if rt.truth(((rt.contains(v_method,"return_type")))) then
                            if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_method,"return_type"),v_class_name}),rt.dict({}))) then
                                rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_method,"return_type")}),rt.dict({}))
                            end
                        end
                    end
                    ::temporary_46::
                end
            end
            if rt.truth(((rt.contains(v_builtin_api,"members")))) then
                for temporary_49 in rt.iter(rt.get(v_builtin_api,"members")) do
                    do
                        v_member = temporary_49
                        if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_member,"type"),v_class_name}),rt.dict({}))) then
                            rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_member,"type")}),rt.dict({}))
                        end
                    end
                    ::temporary_50::
                end
            end
            if rt.truth(((rt.contains(v_builtin_api,"indexing_return_type")))) then
                if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_builtin_api,"indexing_return_type"),v_class_name}),rt.dict({}))) then
                    rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_builtin_api,"indexing_return_type")}),rt.dict({}))
                end
            end
            if rt.truth(((rt.contains(v_builtin_api,"operators")))) then
                for temporary_51 in rt.iter(rt.get(v_builtin_api,"operators")) do
                    do
                        v_operator = temporary_51
                        if rt.truth(((rt.contains(v_operator,"right_type")))) then
                            if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_operator,"right_type"),v_class_name}),rt.dict({}))) then
                                rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_operator,"right_type")}),rt.dict({}))
                            end
                        end
                    end
                    ::temporary_52::
                end
            end
            for temporary_53 in rt.iter(v_fully_used_classes) do
                do
                    v_type_name = temporary_53
                    if rt.truth(((rt.contains(v_used_classes,v_type_name)))) then
                        rt.call(rt.attr(v_used_classes,"remove"),rt.arguments({v_type_name}),rt.dict({}))
                    end
                end
                ::temporary_54::
            end
            v_used_classes = rt.call(v_list,rt.arguments({v_used_classes}),rt.dict({}))
            rt.call(rt.attr(v_used_classes,"sort"),rt.arguments({}),rt.dict({}))
            v_fully_used_classes = rt.call(v_list,rt.arguments({v_fully_used_classes}),rt.dict({}))
            rt.call(rt.attr(v_fully_used_classes,"sort"),rt.arguments({}),rt.dict({}))
            do
                local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
                rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(v_generate_builtin_class_header,rt.arguments({v_builtin_api,v_size,v_used_classes,v_fully_used_classes,v_hooks}),rt.dict({}))}),rt.dict({}))
                rt.close(v_header_file)
            end
            do
                local v_source_file = rt.call(rt.attr(v_source_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
                rt.call(rt.attr(v_source_file,"write"),rt.arguments({rt.call(v_generate_builtin_class_source,rt.arguments({v_builtin_api,v_size,v_used_classes,v_fully_used_classes,v_hooks}),rt.dict({}))}),rt.dict({}))
                rt.close(v_source_file)
            end
        end
        ::temporary_40::
    end
    v_builtin_header_filename = rt.div(v_include_gen_folder,"builtin_types.hpp")
    do
        local v_builtin_header_file = rt.call(rt.attr(v_builtin_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        v_builtin_header = rt.array({})
        rt.call(v_add_header,rt.arguments({"builtin_types.hpp",v_builtin_header}),rt.dict({}))
        rt.call(rt.attr(v_builtin_header,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
        rt.call(rt.attr(v_builtin_header,"append"),rt.arguments({""}),rt.dict({}))
        v_includes = rt.array({})
        for temporary_55 in rt.iter(v_builtin_classes) do
            do
                v_builtin = temporary_55
                rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/variant/" .. rt.str(rt.call(v_camel_to_snake,rt.arguments({v_builtin}),rt.dict({}))) .. ".hpp")}),rt.dict({}))
            end
            ::temporary_56::
        end
        rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
        for temporary_57 in rt.iter(v_includes) do
            do
                v_include = temporary_57
                rt.call(rt.attr(v_builtin_header,"append"),rt.arguments({("#include <" .. rt.str(v_include) .. ">")}),rt.dict({}))
            end
            ::temporary_58::
        end
        rt.call(rt.attr(v_builtin_header,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_builtin_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_builtin_header}),rt.dict({}))}),rt.dict({}))
        rt.close(v_builtin_header_file)
    end
    v_builtin_binds_filename = rt.div(v_include_gen_folder,"builtin_binds.hpp")
    do
        local v_builtin_binds_file = rt.call(rt.attr(v_builtin_binds_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        v_builtin_binds = rt.array({})
        rt.call(v_add_header,rt.arguments({"builtin_binds.hpp",v_builtin_binds}),rt.dict({}))
        rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
        rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({"#include <godot_cpp/variant/builtin_types.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({""}),rt.dict({}))
        for temporary_59 in rt.iter(rt.get(v_api,"builtin_classes")) do
            do
                v_builtin_api = temporary_59
                if rt.truth(rt.call(v_is_included_type,rt.arguments({rt.get(v_builtin_api,"name")}),rt.dict({}))) then
                    if rt.truth(((rt.contains(v_builtin_api,"enums")))) then
                        for temporary_61 in rt.iter(rt.get(v_builtin_api,"enums")) do
                            do
                                v_enum_api = temporary_61
                                rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({("VARIANT_ENUM_CAST(" .. rt.str(rt.get(v_builtin_api,"name")) .. "::" .. rt.str(rt.get(v_enum_api,"name")) .. ");")}),rt.dict({}))
                            end
                            ::temporary_62::
                        end
                    end
                end
            end
            ::temporary_60::
        end
        rt.call(rt.attr(v_builtin_binds,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_builtin_binds_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_builtin_binds}),rt.dict({}))}),rt.dict({}))
        rt.close(v_builtin_binds_file)
    end
    v_builtin_vararg_methods_header = rt.div(v_include_gen_folder,"builtin_vararg_methods.hpp")
    do
        local v_vararg_methods_file = rt.call(rt.attr(v_builtin_vararg_methods_header,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_vararg_methods_file,"write"),rt.arguments({rt.call(v_generate_builtin_class_vararg_method_implements_header,rt.arguments({rt.get(v_api,"builtin_classes")}),rt.dict({}))}),rt.dict({}))
        rt.close(v_vararg_methods_file)
    end
end)
v_generate_builtin_class_vararg_method_implements_header =
rt.fn({"builtin_classes"},{rt._missing},function(v_builtin_classes)
    local v_builtin_api,v_class_name,v_method,v_result
    v_result = rt.array({})
    rt.call(v_add_header,rt.arguments({"builtin_vararg_methods.hpp",v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    for temporary_63 in rt.iter(v_builtin_classes) do
        do
            v_builtin_api = temporary_63
            if rt.truth(((not rt.contains(v_builtin_api,"methods")))) then
                goto temporary_64
            end
            v_class_name = rt.get(v_builtin_api,"name")
            for temporary_65 in rt.iter(rt.get(v_builtin_api,"methods")) do
                do
                    v_method = temporary_65
                    if rt.truth(not rt.truth(rt.get(v_method,"is_vararg"))) then
                        goto temporary_66
                    end
                    v_result = rt.add(v_result,rt.call(v_make_varargs_template,rt.arguments({v_method,rt.logical(((rt.contains(v_method,"is_static"))),function() return rt.get(v_method,"is_static") end,true),v_class_name,false,true}),rt.dict({})))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
                end
                ::temporary_66::
            end
        end
        ::temporary_64::
    end
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_builtin_class_header =
rt.fn({"builtin_api","size","used_classes","fully_used_classes","hooks"},{rt._missing,rt._missing,rt._missing,rt._missing,rt._none},function(v_builtin_api,v_size,v_used_classes,v_fully_used_classes,v_hooks)
    local v_alignment,v_axis_constants_count,v_class_name,v_constant,v_constructor,v_copy_constructor_index,v_include,v_includes,v_init_list,v_iterators,v_member,v_method,v_method_arguments,v_method_list,v_method_signature,v_operator,v_result,v_return_type,v_snake_class_name,v_type_name,v_vararg
    v_result = rt.array({})
    v_class_name = rt.get(v_builtin_api,"name")
    v_snake_class_name = rt.call(rt.attr(rt.call(v_camel_to_snake,rt.arguments({v_class_name}),rt.dict({})),"upper"),rt.arguments({}),rt.dict({}))
    rt.call(v_add_header,rt.arguments({(rt.str(rt.call(rt.attr(v_snake_class_name,"lower"),rt.arguments({}),rt.dict({}))) .. ".hpp"),v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/defs.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/math_defs.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth((((v_class_name=="String")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/classes/global_constants.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/char_string.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/char_utils.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="PackedStringArray")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/string.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="PackedColorArray")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/color.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="PackedVector2Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/vector2.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="PackedVector3Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/vector3.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="PackedVector4Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/vector4.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth(rt.logical(rt.call(v_is_packed_array,rt.arguments({v_class_name}),rt.dict({})),function() return (((v_class_name=="Array"))) end,false)) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/error_macros.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <initializer_list>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/array_helpers.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Callable")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/variant/callable_custom.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((rt.call(v_len,rt.arguments({v_fully_used_classes}),rt.dict({}))>0)))) then
        v_includes = rt.array({})
        for temporary_67 in rt.iter(v_fully_used_classes) do
            do
                v_include = temporary_67
                if rt.truth((((v_include=="TypedArray")))) then
                    rt.call(rt.attr(v_includes,"append"),rt.arguments({"godot_cpp/variant/typed_array.hpp"}),rt.dict({}))
                else
                    if rt.truth((((v_include=="TypedDictionary")))) then
                        rt.call(rt.attr(v_includes,"append"),rt.arguments({"godot_cpp/variant/typed_dictionary.hpp"}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/" .. rt.str(rt.call(v_get_include_path,rt.arguments({v_include}),rt.dict({}))))}),rt.dict({}))
                    end
                end
            end
            ::temporary_68::
        end
        rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
        for temporary_69 in rt.iter(v_includes) do
            do
                v_include = temporary_69
                rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <" .. rt.str(v_include) .. ">")}),rt.dict({}))
            end
            ::temporary_70::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <gdextension_interface.h>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    for temporary_71 in rt.iter(v_used_classes) do
        do
            v_type_name = temporary_71
            if rt.truth(rt.call(v_is_struct_type,rt.arguments({v_type_name}),rt.dict({}))) then
                rt.call(rt.attr(v_result,"append"),rt.arguments({("struct " .. rt.str(v_type_name) .. ";")}),rt.dict({}))
            else
                rt.call(rt.attr(v_result,"append"),rt.arguments({("class " .. rt.str(v_type_name) .. ";")}),rt.dict({}))
            end
        end
        ::temporary_72::
    end
    if rt.truth((((rt.call(v_len,rt.arguments({v_used_classes}),rt.dict({}))>0)))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({("class " .. rt.str(v_class_name) .. " {")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic constexpr size_t " .. rt.str(v_snake_class_name) .. "_SIZE = " .. rt.str(v_size) .. ";")}),rt.dict({}))
    v_alignment = rt.choose((((v_size>=8))),function() return 8 end,function() return 4 end)
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\talignas(" .. rt.str(v_alignment) .. ") uint8_t opaque[" .. rt.str(v_snake_class_name) .. "_SIZE] = {};")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tfriend class Variant;"}),rt.dict({}))
    if rt.truth((((v_class_name=="String")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tfriend class StringName;"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic struct _MethodBindings {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionTypeFromVariantConstructorFunc from_variant_constructor;"}),rt.dict({}))
    if rt.truth(((rt.contains(v_builtin_api,"constructors")))) then
        for temporary_73 in rt.iter(rt.get(v_builtin_api,"constructors")) do
            do
                v_constructor = temporary_73
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrConstructor constructor_" .. rt.str(rt.get(v_constructor,"index")) .. ";")}),rt.dict({}))
            end
            ::temporary_74::
        end
    end
    if rt.truth(rt.get(v_builtin_api,"has_destructor")) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrDestructor destructor;"}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_builtin_api,"methods")))) then
        for temporary_75 in rt.iter(rt.get(v_builtin_api,"methods")) do
            do
                v_method = temporary_75
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrBuiltInMethod method_" .. rt.str(rt.get(v_method,"name")) .. ";")}),rt.dict({}))
            end
            ::temporary_76::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"members")))) then
        for temporary_77 in rt.iter(rt.get(v_builtin_api,"members")) do
            do
                v_member = temporary_77
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrSetter member_" .. rt.str(rt.get(v_member,"name")) .. "_setter;")}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrGetter member_" .. rt.str(rt.get(v_member,"name")) .. "_getter;")}),rt.dict({}))
            end
            ::temporary_78::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"indexing_return_type")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrIndexedSetter indexed_setter;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrIndexedGetter indexed_getter;"}),rt.dict({}))
    end
    if rt.truth(rt.logical(((rt.contains(v_builtin_api,"is_keyed"))),function() return rt.get(v_builtin_api,"is_keyed") end,true)) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrKeyedSetter keyed_setter;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrKeyedGetter keyed_getter;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tGDExtensionPtrKeyedChecker keyed_checker;"}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_builtin_api,"operators")))) then
        for temporary_79 in rt.iter(rt.get(v_builtin_api,"operators")) do
            do
                v_operator = temporary_79
                if rt.truth(((rt.contains(v_operator,"right_type")))) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrOperatorEvaluator operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "_" .. rt.str(rt.get(v_operator,"right_type")) .. ";")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionPtrOperatorEvaluator operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. ";")}),rt.dict({}))
                end
            end
            ::temporary_80::
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t} _method_bindings;"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic void init_bindings();"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic void _init_bindings_constructors_destructor();"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const Variant *p_variant);")}),rt.dict({}))
    if rt.truth((((v_class_name=="Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst Variant *ptr() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tVariant *ptrw();"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"public:"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_FORCE_INLINE_ GDExtensionTypePtr _native_ptr() const { return const_cast<uint8_t(*)[" .. rt.str(v_snake_class_name) .. "_SIZE]>(&opaque); }")}),rt.dict({}))
    v_copy_constructor_index = (-1)
    if rt.truth(((rt.contains(v_builtin_api,"constructors")))) then
        for temporary_81 in rt.iter(rt.get(v_builtin_api,"constructors")) do
            do
                v_constructor = temporary_81
                v_method_signature = ("\t" .. rt.str(v_class_name) .. "(")
                if rt.truth(((rt.contains(v_constructor,"arguments")))) then
                    v_method_signature = rt.add(v_method_signature,rt.call(v_make_function_parameters,rt.arguments({rt.get(v_constructor,"arguments")}),rt.dict({{"include_default",true},{"for_builtin",true}})))
                    if rt.truth(rt.logical((((rt.call(v_len,rt.arguments({rt.get(v_constructor,"arguments")}),rt.dict({}))==1))),function() return (((rt.get(rt.get(rt.get(v_constructor,"arguments"),0),"type")==v_class_name))) end,true)) then
                        v_copy_constructor_index = rt.get(v_constructor,"index")
                    end
                end
                v_method_signature = rt.add(v_method_signature,");")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
            end
            ::temporary_82::
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(" .. rt.str(v_class_name) .. " &&p_other);")}),rt.dict({}))
    if rt.truth(rt.logical((((v_class_name=="String"))),function() return rt.logical((((v_class_name=="StringName"))),function() return (((v_class_name=="NodePath"))) end,false) end,false)) then
        if rt.truth((((v_class_name=="StringName")))) then
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const char *p_from, bool p_static = false);")}),rt.dict({}))
        else
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const char *p_from);")}),rt.dict({}))
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const wchar_t *p_from);")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const char16_t *p_from);")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "(const char32_t *p_from);")}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Callable")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCallable(CallableCustom *p_custom);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCallableCustom *get_custom() const;"}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_builtin_api,"constants")))) then
        v_axis_constants_count = 0
        for temporary_83 in rt.iter(rt.get(v_builtin_api,"constants")) do
            do
                v_constant = temporary_83
                if rt.truth(rt.logical((((v_class_name=="Vector3"))),function() return rt.call(rt.attr(rt.get(v_constant,"name"),"startswith"),rt.arguments({"AXIS"}),rt.dict({})) end,true)) then
                    if rt.truth((((v_axis_constants_count==0)))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tenum Axis {"}),rt.dict({}))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t" .. rt.str(rt.get(v_constant,"name")) .. " = " .. rt.str(rt.get(v_constant,"value")) .. ",")}),rt.dict({}))
                    v_axis_constants_count = rt.add(v_axis_constants_count,1)
                    if rt.truth((((v_axis_constants_count==3)))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t};"}),rt.dict({}))
                    end
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic const " .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_constant,"type")}),rt.dict({}))) .. " " .. rt.str(rt.get(v_constant,"name")) .. ";")}),rt.dict({}))
                end
            end
            ::temporary_84::
        end
    end
    if rt.truth(rt.get(v_builtin_api,"has_destructor")) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t~" .. rt.str(v_class_name) .. "();")}),rt.dict({}))
    end
    v_method_list = rt.array({})
    if rt.truth(((rt.contains(v_builtin_api,"methods")))) then
        for temporary_85 in rt.iter(rt.get(v_builtin_api,"methods")) do
            do
                v_method = temporary_85
                rt.call(rt.attr(v_method_list,"append"),rt.arguments({rt.get(v_method,"name")}),rt.dict({}))
                v_vararg = rt.get(v_method,"is_vararg")
                if rt.truth(v_vararg) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename... Args>"}),rt.dict({}))
                end
                v_method_signature = "\t"
                if rt.truth(rt.logical(((rt.contains(v_method,"is_static"))),function() return rt.get(v_method,"is_static") end,true)) then
                    v_method_signature = rt.add(v_method_signature,"static ")
                end
                if rt.truth(((rt.contains(v_method,"return_type")))) then
                    v_method_signature = rt.add(v_method_signature,(rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_method,"return_type")}),rt.dict({})))))
                    if rt.truth(not rt.truth(rt.call(rt.attr(v_method_signature,"endswith"),rt.arguments({"*"}),rt.dict({})))) then
                        v_method_signature = rt.add(v_method_signature," ")
                    end
                else
                    v_method_signature = rt.add(v_method_signature,"void ")
                end
                v_method_signature = rt.add(v_method_signature,(rt.str(rt.get(v_method,"name")) .. "("))
                v_method_arguments = rt.array({})
                if rt.truth(((rt.contains(v_method,"arguments")))) then
                    v_method_arguments = rt.get(v_method,"arguments")
                end
                v_method_signature = rt.add(v_method_signature,rt.call(v_make_function_parameters,rt.arguments({v_method_arguments}),rt.dict({{"include_default",true},{"for_builtin",true},{"is_vararg",v_vararg}})))
                v_method_signature = rt.add(v_method_signature,")")
                if rt.truth(rt.get(v_method,"is_const")) then
                    v_method_signature = rt.add(v_method_signature," const")
                end
                v_method_signature = rt.add(v_method_signature,";")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
            end
            ::temporary_86::
        end
    end
    if rt.truth((((v_class_name=="String")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic String utf8(const char *p_from, int64_t p_len = -1);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tError parse_utf8(const char *p_from, int64_t p_len = -1);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic String utf16(const char16_t *p_from, int64_t p_len = -1);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tError parse_utf16(const char16_t *p_from, int64_t p_len = -1, bool p_default_little_endian = true);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCharString utf8() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCharString ascii() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tChar16String utf16() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tChar32String utf32() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCharWideString wide_string() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic String num_real(double p_num, bool p_trailing = true);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tError resize(int64_t p_size);"}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_builtin_api,"members")))) then
        for temporary_87 in rt.iter(rt.get(v_builtin_api,"members")) do
            do
                v_member = temporary_87
                if rt.truth(((not rt.contains(v_method_list,("get_" .. rt.str(rt.get(v_member,"name"))))))) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) .. " get_" .. rt.str(rt.get(v_member,"name")) .. "() const;")}),rt.dict({}))
                end
                if rt.truth(((not rt.contains(v_method_list,("set_" .. rt.str(rt.get(v_member,"name"))))))) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tvoid set_" .. rt.str(rt.get(v_member,"name")) .. "(" .. rt.str(rt.call(v_type_for_parameter,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) .. "value);")}),rt.dict({}))
                end
            end
            ::temporary_88::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"operators")))) then
        for temporary_89 in rt.iter(rt.get(v_builtin_api,"operators")) do
            do
                v_operator = temporary_89
                if rt.truth(rt.call(v_is_valid_cpp_operator,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) then
                    if rt.truth(((rt.contains(v_operator,"right_type")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))) .. " operator" .. rt.str(rt.call(v_get_operator_cpp_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "(" .. rt.str(rt.call(v_type_for_parameter,rt.arguments({rt.get(v_operator,"right_type")}),rt.dict({}))) .. "p_other) const;")}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))) .. " operator" .. rt.str(rt.call(v_get_operator_cpp_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "() const;")}),rt.dict({}))
                    end
                end
            end
            ::temporary_90::
        end
    end
    if rt.truth((((v_copy_constructor_index>=0)))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. " &operator=(const " .. rt.str(v_class_name) .. " &p_other);")}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. " &operator=(" .. rt.str(v_class_name) .. " &&p_other);")}),rt.dict({}))
    if rt.truth((((v_class_name=="String")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator=(const char *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator=(const wchar_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator=(const char16_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator=(const char32_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator==(const char *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator==(const wchar_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator==(const char16_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator==(const char32_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator!=(const char *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator!=(const wchar_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator!=(const char16_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tbool operator!=(const char32_t *p_str) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString operator+(const char *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString operator+(const wchar_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString operator+(const char16_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString operator+(const char32_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString operator+(char32_t p_char);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator+=(const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator+=(char32_t p_char);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator+=(const char *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator+=(const wchar_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString &operator+=(const char32_t *p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst char32_t &operator[](int64_t p_index) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tchar32_t &operator[](int64_t p_index);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst char32_t *ptr() const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tchar32_t *ptrw();"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename... Args>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic Array make(Args... p_args) {"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\treturn helpers::append_all(Array(), p_args...);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}"}),rt.dict({}))
    end
    if rt.truth(rt.call(v_is_packed_array,rt.arguments({v_class_name}),rt.dict({}))) then
        v_return_type = rt.call(v_correct_type,rt.arguments({rt.get(v_builtin_api,"indexing_return_type")}),rt.dict({}))
        if rt.truth((((v_class_name=="PackedByteArray")))) then
            v_return_type = "uint8_t"
        else
            if rt.truth((((v_class_name=="PackedInt32Array")))) then
                v_return_type = "int32_t"
            else
                if rt.truth((((v_class_name=="PackedFloat32Array")))) then
                    v_return_type = "float"
                end
            end
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tconst " .. rt.str(v_return_type) .. " &operator[](int64_t p_index) const;")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_return_type) .. " &operator[](int64_t p_index);")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tconst " .. rt.str(v_return_type) .. " *ptr() const;")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_return_type) .. " *ptrw();")}),rt.dict({}))
        v_iterators = "\n\tstruct Iterator {\n\t\t_FORCE_INLINE_ $TYPE &operator*() const {\n\t\t\treturn *elem_ptr;\n\t\t}\n\t\t_FORCE_INLINE_ $TYPE *operator->() const { return elem_ptr; }\n\t\t_FORCE_INLINE_ Iterator &operator++() {\n\t\t\telem_ptr++;\n\t\t\treturn *this;\n\t\t}\n\t\t_FORCE_INLINE_ Iterator &operator--() {\n\t\t\telem_ptr--;\n\t\t\treturn *this;\n\t\t}\n\n\t\t_FORCE_INLINE_ bool operator==(const Iterator &b) const { return elem_ptr == b.elem_ptr; }\n\t\t_FORCE_INLINE_ bool operator!=(const Iterator &b) const { return elem_ptr != b.elem_ptr; }\n\n\t\tIterator($TYPE *p_ptr) { elem_ptr = p_ptr; }\n\t\tIterator() {}\n\t\tIterator(const Iterator &p_it) { elem_ptr = p_it.elem_ptr; }\n\n\tprivate:\n\t\t$TYPE *elem_ptr = nullptr;\n\t};\n\n\tstruct ConstIterator {\n\t\t_FORCE_INLINE_ const $TYPE &operator*() const {\n\t\t\treturn *elem_ptr;\n\t\t}\n\t\t_FORCE_INLINE_ const $TYPE *operator->() const { return elem_ptr; }\n\t\t_FORCE_INLINE_ ConstIterator &operator++() {\n\t\t\telem_ptr++;\n\t\t\treturn *this;\n\t\t}\n\t\t_FORCE_INLINE_ ConstIterator &operator--() {\n\t\t\telem_ptr--;\n\t\t\treturn *this;\n\t\t}\n\n\t\t_FORCE_INLINE_ bool operator==(const ConstIterator &b) const { return elem_ptr == b.elem_ptr; }\n\t\t_FORCE_INLINE_ bool operator!=(const ConstIterator &b) const { return elem_ptr != b.elem_ptr; }\n\n\t\tConstIterator(const $TYPE *p_ptr) { elem_ptr = p_ptr; }\n\t\tConstIterator() {}\n\t\tConstIterator(const ConstIterator &p_it) { elem_ptr = p_it.elem_ptr; }\n\n\tprivate:\n\t\tconst $TYPE *elem_ptr = nullptr;\n\t};\n\n\t_FORCE_INLINE_ Iterator begin() {\n\t\treturn Iterator(ptrw());\n\t}\n\t_FORCE_INLINE_ Iterator end() {\n\t\treturn Iterator(ptrw() + size());\n\t}\n\n\t_FORCE_INLINE_ ConstIterator begin() const {\n\t\treturn ConstIterator(ptr());\n\t}\n\t_FORCE_INLINE_ ConstIterator end() const {\n\t\treturn ConstIterator(ptr() + size());\n\t}"
        rt.call(rt.attr(v_result,"append"),rt.arguments({rt.call(rt.attr(v_iterators,"replace"),rt.arguments({"$TYPE",v_return_type}),rt.dict({}))}),rt.dict({}))
        v_init_list = "\n\t_FORCE_INLINE_ $CLASS(std::initializer_list<$TYPE> p_init) {\n\t\tERR_FAIL_COND(resize(p_init.size()) != 0);\n\n\t\tsize_t i = 0;\n\t\tfor (const $TYPE &element : p_init) {\n\t\t\tset(i++, element);\n\t\t}\n\t}"
        rt.call(rt.attr(v_result,"append"),rt.arguments({rt.call(rt.attr(rt.call(rt.attr(v_init_list,"replace"),rt.arguments({"$TYPE",v_return_type}),rt.dict({})),"replace"),rt.arguments({"$CLASS",v_class_name}),rt.dict({}))}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Array")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst Variant &operator[](int64_t p_index) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tVariant &operator[](int64_t p_index);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tvoid set_typed(uint32_t p_type, const StringName &p_class_name, const Variant &p_script);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\n\tstruct Iterator {\n\t\t_FORCE_INLINE_ Variant &operator*() const;\n\t\t_FORCE_INLINE_ Variant *operator->() const;\n\t\t_FORCE_INLINE_ Iterator &operator++();\n\t\t_FORCE_INLINE_ Iterator &operator--();\n\n\t\t_FORCE_INLINE_ bool operator==(const Iterator &b) const { return elem_ptr == b.elem_ptr; }\n\t\t_FORCE_INLINE_ bool operator!=(const Iterator &b) const { return elem_ptr != b.elem_ptr; }\n\n\t\tIterator(Variant *p_ptr) { elem_ptr = p_ptr; }\n\t\tIterator() {}\n\t\tIterator(const Iterator &p_it) { elem_ptr = p_it.elem_ptr; }\n\n\tprivate:\n\t\tVariant *elem_ptr = nullptr;\n\t};\n\n\tstruct ConstIterator {\n\t\t_FORCE_INLINE_ const Variant &operator*() const;\n\t\t_FORCE_INLINE_ const Variant *operator->() const;\n\t\t_FORCE_INLINE_ ConstIterator &operator++();\n\t\t_FORCE_INLINE_ ConstIterator &operator--();\n\n\t\t_FORCE_INLINE_ bool operator==(const ConstIterator &b) const { return elem_ptr == b.elem_ptr; }\n\t\t_FORCE_INLINE_ bool operator!=(const ConstIterator &b) const { return elem_ptr != b.elem_ptr; }\n\n\t\tConstIterator(const Variant *p_ptr) { elem_ptr = p_ptr; }\n\t\tConstIterator() {}\n\t\tConstIterator(const ConstIterator &p_it) { elem_ptr = p_it.elem_ptr; }\n\n\tprivate:\n\t\tconst Variant *elem_ptr = nullptr;\n\t};\n\n\t_FORCE_INLINE_ Iterator begin();\n\t_FORCE_INLINE_ Iterator end();\n\n\t_FORCE_INLINE_ ConstIterator begin() const;\n\t_FORCE_INLINE_ ConstIterator end() const;\n\t"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t_FORCE_INLINE_ Array(std::initializer_list<Variant> p_init);"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Dictionary")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst Variant &operator[](const Variant &p_key) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tVariant &operator[](const Variant &p_key);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#if GODOT_VERSION_MINOR >= 4"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tvoid set_typed(uint32_t p_key_type, const StringName &p_key_class_name, const Variant &p_key_script, uint32_t p_value_type, const StringName &p_value_class_name, const Variant &p_value_script);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"};"}),rt.dict({}))
    if rt.truth((((v_class_name=="String")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator==(const char *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator==(const wchar_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator==(const char16_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator==(const char32_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator!=(const char *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator!=(const wchar_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator!=(const char16_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"bool operator!=(const char32_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String operator+(const char *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String operator+(const wchar_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String operator+(const char16_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String operator+(const char32_t *p_chr, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String operator+(char32_t p_char, const String &p_str);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String itos(int64_t p_val);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String uitos(uint64_t p_val);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String rtos(double p_val);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"String rtoss(double p_val);"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_result = rt.call(rt.attr(v_hooks,"alter_builtin_class_header"),rt.arguments({v_builtin_api,v_result}),rt.dict({}))
    end
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_builtin_class_source =
rt.fn({"builtin_api","size","used_classes","fully_used_classes","hooks"},{rt._missing,rt._missing,rt._missing,rt._missing,rt._none},function(v_builtin_api,v_size,v_used_classes,v_fully_used_classes,v_hooks)
    local v_arg_name,v_argument,v_arguments,v_class_name,v_constructor,v_copy_constructor_index,v_encode,v_enum_type_name,v_included,v_includes,v_is_ref,v_member,v_method,v_method_call,v_method_list,v_method_signature,v_operator,v_result,v_return_type,v_right_type_variant_type,v_snake_class_name
    v_result = rt.array({})
    v_class_name = rt.get(v_builtin_api,"name")
    v_snake_class_name = rt.call(v_camel_to_snake,rt.arguments({v_class_name}),rt.dict({}))
    v_enum_type_name = ("GDEXTENSION_VARIANT_TYPE_" .. rt.str(rt.call(rt.attr(v_snake_class_name,"upper"),rt.arguments({}),rt.dict({}))))
    rt.call(v_add_header,rt.arguments({(rt.str(v_snake_class_name) .. ".cpp"),v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <godot_cpp/variant/" .. rt.str(v_snake_class_name) .. ".hpp>")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/binder_common.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/godot.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth((((rt.call(v_len,rt.arguments({v_used_classes}),rt.dict({}))>0)))) then
        v_includes = rt.array({})
        for temporary_91 in rt.iter(v_used_classes) do
            do
                v_included = temporary_91
                rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/" .. rt.str(rt.call(v_get_include_path,rt.arguments({v_included}),rt.dict({}))))}),rt.dict({}))
            end
            ::temporary_92::
        end
        rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
        for temporary_93 in rt.iter(v_includes) do
            do
                v_included = temporary_93
                rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <" .. rt.str(v_included) .. ">")}),rt.dict({}))
            end
            ::temporary_94::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/builtin_ptrcall.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <utility>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. "::_MethodBindings " .. rt.str(v_class_name) .. "::_method_bindings;")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("void " .. rt.str(v_class_name) .. "::_init_bindings_constructors_destructor() {")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.from_variant_constructor = ::godot::gdextension_interface::get_variant_to_type_constructor(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
    if rt.truth(((rt.contains(v_builtin_api,"constructors")))) then
        for temporary_95 in rt.iter(rt.get(v_builtin_api,"constructors")) do
            do
                v_constructor = temporary_95
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.constructor_" .. rt.str(rt.get(v_constructor,"index")) .. " = ::godot::gdextension_interface::variant_get_ptr_constructor(" .. rt.str(v_enum_type_name) .. ", " .. rt.str(rt.get(v_constructor,"index")) .. ");")}),rt.dict({}))
            end
            ::temporary_96::
        end
    end
    if rt.truth(rt.get(v_builtin_api,"has_destructor")) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.destructor = ::godot::gdextension_interface::variant_get_ptr_destructor(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("void " .. rt.str(v_class_name) .. "::init_bindings() {")}),rt.dict({}))
    if rt.truth((((v_class_name=="StringName")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString::_init_bindings_constructors_destructor();"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_class_name) .. "::_init_bindings_constructors_destructor();")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tStringName _gde_name;"}),rt.dict({}))
    if rt.truth(((rt.contains(v_builtin_api,"methods")))) then
        for temporary_97 in rt.iter(rt.get(v_builtin_api,"methods")) do
            do
                v_method = temporary_97
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_gde_name = StringName(\"" .. rt.str(rt.get(v_method,"name")) .. "\");")}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.method_" .. rt.str(rt.get(v_method,"name")) .. " = ::godot::gdextension_interface::variant_get_ptr_builtin_method(" .. rt.str(v_enum_type_name) .. ", _gde_name._native_ptr(), " .. rt.str(rt.get(v_method,"hash")) .. ");")}),rt.dict({}))
            end
            ::temporary_98::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"members")))) then
        for temporary_99 in rt.iter(rt.get(v_builtin_api,"members")) do
            do
                v_member = temporary_99
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_gde_name = StringName(\"" .. rt.str(rt.get(v_member,"name")) .. "\");")}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.member_" .. rt.str(rt.get(v_member,"name")) .. "_setter = ::godot::gdextension_interface::variant_get_ptr_setter(" .. rt.str(v_enum_type_name) .. ", _gde_name._native_ptr());")}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.member_" .. rt.str(rt.get(v_member,"name")) .. "_getter = ::godot::gdextension_interface::variant_get_ptr_getter(" .. rt.str(v_enum_type_name) .. ", _gde_name._native_ptr());")}),rt.dict({}))
            end
            ::temporary_100::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"indexing_return_type")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.indexed_setter = ::godot::gdextension_interface::variant_get_ptr_indexed_setter(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.indexed_getter = ::godot::gdextension_interface::variant_get_ptr_indexed_getter(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
    end
    if rt.truth(rt.logical(((rt.contains(v_builtin_api,"is_keyed"))),function() return rt.get(v_builtin_api,"is_keyed") end,true)) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.keyed_setter = ::godot::gdextension_interface::variant_get_ptr_keyed_setter(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.keyed_getter = ::godot::gdextension_interface::variant_get_ptr_keyed_getter(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.keyed_checker = ::godot::gdextension_interface::variant_get_ptr_keyed_checker(" .. rt.str(v_enum_type_name) .. ");")}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_builtin_api,"operators")))) then
        for temporary_101 in rt.iter(rt.get(v_builtin_api,"operators")) do
            do
                v_operator = temporary_101
                if rt.truth(((rt.contains(v_operator,"right_type")))) then
                    if rt.truth((((rt.get(v_operator,"right_type")=="Variant")))) then
                        v_right_type_variant_type = "GDEXTENSION_VARIANT_TYPE_NIL"
                    else
                        v_right_type_variant_type = ("GDEXTENSION_VARIANT_TYPE_" .. rt.str(rt.call(rt.attr(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_operator,"right_type")}),rt.dict({})),"upper"),rt.arguments({}),rt.dict({}))))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "_" .. rt.str(rt.get(v_operator,"right_type")) .. " = ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(GDEXTENSION_VARIANT_OP_" .. rt.str(rt.call(rt.attr(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({})),"upper"),rt.arguments({}),rt.dict({}))) .. ", " .. rt.str(v_enum_type_name) .. ", " .. rt.str(v_right_type_variant_type) .. ");")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. " = ::godot::gdextension_interface::variant_get_ptr_operator_evaluator(GDEXTENSION_VARIANT_OP_" .. rt.str(rt.call(rt.attr(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({})),"upper"),rt.arguments({}),rt.dict({}))) .. ", " .. rt.str(v_enum_type_name) .. ", GDEXTENSION_VARIANT_TYPE_NIL);")}),rt.dict({}))
                end
            end
            ::temporary_102::
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    v_copy_constructor_index = (-1)
    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. "::" .. rt.str(v_class_name) .. "(const Variant *p_variant) {")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t_method_bindings.from_variant_constructor(&opaque, p_variant->_native_ptr());"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(((rt.contains(v_builtin_api,"constructors")))) then
        for temporary_103 in rt.iter(rt.get(v_builtin_api,"constructors")) do
            do
                v_constructor = temporary_103
                v_method_signature = (rt.str(v_class_name) .. "::" .. rt.str(v_class_name) .. "(")
                if rt.truth(((rt.contains(v_constructor,"arguments")))) then
                    v_method_signature = rt.add(v_method_signature,rt.call(v_make_function_parameters,rt.arguments({rt.get(v_constructor,"arguments")}),rt.dict({{"include_default",false},{"for_builtin",true}})))
                end
                v_method_signature = rt.add(v_method_signature,") {")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
                v_method_call = ("\t::godot::internal::_call_builtin_constructor(_method_bindings.constructor_" .. rt.str(rt.get(v_constructor,"index")) .. ", &opaque")
                if rt.truth(((rt.contains(v_constructor,"arguments")))) then
                    if rt.truth(rt.logical((((rt.call(v_len,rt.arguments({rt.get(v_constructor,"arguments")}),rt.dict({}))==1))),function() return (((rt.get(rt.get(rt.get(v_constructor,"arguments"),0),"type")==v_class_name))) end,true)) then
                        v_copy_constructor_index = rt.get(v_constructor,"index")
                    end
                    v_method_call = rt.add(v_method_call,", ")
                    v_arguments = rt.array({})
                    for temporary_105 in rt.iter(rt.get(v_constructor,"arguments")) do
                        do
                            v_argument = temporary_105
                            v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),1)
                            v_result = rt.add(v_result,v_encode)
                            rt.call(rt.attr(v_arguments,"append"),rt.arguments({v_arg_name}),rt.dict({}))
                        end
                        ::temporary_106::
                    end
                    v_method_call = rt.add(v_method_call,rt.call(rt.attr(", ","join"),rt.arguments({v_arguments}),rt.dict({})))
                end
                v_method_call = rt.add(v_method_call,");")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_call}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_104::
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. "::" .. rt.str(v_class_name) .. "(" .. rt.str(v_class_name) .. " &&p_other) {")}),rt.dict({}))
    if rt.truth(rt.logical(rt.call(v_needs_copy_instead_of_move,rt.arguments({v_class_name}),rt.dict({})),function() return (((v_copy_constructor_index>=0))) end,true)) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t::godot::internal::_call_builtin_constructor(_method_bindings.constructor_" .. rt.str(v_copy_constructor_index) .. ", &opaque, &p_other);")}),rt.dict({}))
    else
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstd::swap(opaque, p_other.opaque);"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(rt.get(v_builtin_api,"has_destructor")) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. "::~" .. rt.str(v_class_name) .. "() {")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t_method_bindings.destructor(&opaque);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    v_method_list = rt.array({})
    if rt.truth(((rt.contains(v_builtin_api,"methods")))) then
        for temporary_107 in rt.iter(rt.get(v_builtin_api,"methods")) do
            do
                v_method = temporary_107
                rt.call(rt.attr(v_method_list,"append"),rt.arguments({rt.get(v_method,"name")}),rt.dict({}))
                if rt.truth(rt.logical(((rt.contains(v_method,"is_vararg"))),function() return rt.get(v_method,"is_vararg") end,true)) then
                    goto temporary_108
                end
                v_method_signature = rt.call(v_make_signature,rt.arguments({v_class_name,v_method}),rt.dict({{"for_builtin",true}}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({rt.add(v_method_signature," {")}),rt.dict({}))
                v_method_call = "\t"
                v_is_ref = false
                if rt.truth(((rt.contains(v_method,"return_type")))) then
                    v_return_type = rt.get(v_method,"return_type")
                    if rt.truth(rt.call(v_is_enum,rt.arguments({v_return_type}),rt.dict({}))) then
                        v_method_call = rt.add(v_method_call,("return (" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type}),rt.dict({}))}),rt.dict({}))) .. ")::godot::internal::_call_builtin_method_ptr_ret<int64_t>("))
                    else
                        if rt.truth(rt.logical(rt.call(v_is_pod_type,rt.arguments({v_return_type}),rt.dict({})),function() return rt.call(v_is_variant,rt.arguments({v_return_type}),rt.dict({})) end,false)) then
                            v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_builtin_method_ptr_ret<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type}),rt.dict({}))}),rt.dict({}))) .. ">("))
                        else
                            if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_return_type}),rt.dict({}))) then
                                v_method_call = rt.add(v_method_call,("return Ref<" .. rt.str(v_return_type) .. ">::_gde_internal_constructor(::godot::internal::_call_builtin_method_ptr_ret_obj<" .. rt.str(v_return_type) .. ">("))
                                v_is_ref = true
                            else
                                v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_builtin_method_ptr_ret_obj<" .. rt.str(v_return_type) .. ">("))
                            end
                        end
                    end
                else
                    v_method_call = rt.add(v_method_call,"::godot::internal::_call_builtin_method_ptr_no_ret(")
                end
                v_method_call = rt.add(v_method_call,("_method_bindings.method_" .. rt.str(rt.get(v_method,"name")) .. ", "))
                if rt.truth(rt.logical(((rt.contains(v_method,"is_static"))),function() return rt.get(v_method,"is_static") end,true)) then
                    v_method_call = rt.add(v_method_call,"nullptr")
                else
                    v_method_call = rt.add(v_method_call,"(GDExtensionTypePtr)&opaque")
                end
                if rt.truth(((rt.contains(v_method,"arguments")))) then
                    v_arguments = rt.array({})
                    v_method_call = rt.add(v_method_call,", ")
                    for temporary_109 in rt.iter(rt.get(v_method,"arguments")) do
                        do
                            v_argument = temporary_109
                            v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),1)
                            v_result = rt.add(v_result,v_encode)
                            rt.call(rt.attr(v_arguments,"append"),rt.arguments({v_arg_name}),rt.dict({}))
                        end
                        ::temporary_110::
                    end
                    v_method_call = rt.add(v_method_call,rt.call(rt.attr(", ","join"),rt.arguments({v_arguments}),rt.dict({})))
                end
                if rt.truth(v_is_ref) then
                    v_method_call = rt.add(v_method_call,")")
                end
                v_method_call = rt.add(v_method_call,");")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_call}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_108::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"members")))) then
        for temporary_111 in rt.iter(rt.get(v_builtin_api,"members")) do
            do
                v_member = temporary_111
                if rt.truth(((not rt.contains(v_method_list,("get_" .. rt.str(rt.get(v_member,"name"))))))) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) .. " " .. rt.str(v_class_name) .. "::get_" .. rt.str(rt.get(v_member,"name")) .. "() const {")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\treturn ::godot::internal::_call_builtin_ptr_getter<" .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) .. ">(_method_bindings.member_" .. rt.str(rt.get(v_member,"name")) .. "_getter, (GDExtensionConstTypePtr)&opaque);")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                end
                if rt.truth(((not rt.contains(v_method_list,("set_" .. rt.str(rt.get(v_member,"name"))))))) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("void " .. rt.str(v_class_name) .. "::set_" .. rt.str(rt.get(v_member,"name")) .. "(" .. rt.str(rt.call(v_type_for_parameter,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) .. "value) {")}),rt.dict({}))
                    v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"value",rt.get(v_member,"type"),rt._none}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"value",rt.get(v_member,"type"),rt._none}),rt.dict({})),1)
                    v_result = rt.add(v_result,v_encode)
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.member_" .. rt.str(rt.get(v_member,"name")) .. "_setter((GDExtensionConstTypePtr)&opaque, (GDExtensionConstTypePtr)" .. rt.str(v_arg_name) .. ");")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                end
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_112::
        end
    end
    if rt.truth(((rt.contains(v_builtin_api,"operators")))) then
        for temporary_113 in rt.iter(rt.get(v_builtin_api,"operators")) do
            do
                v_operator = temporary_113
                if rt.truth(rt.call(v_is_valid_cpp_operator,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) then
                    if rt.truth(((rt.contains(v_operator,"right_type")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))) .. " " .. rt.str(v_class_name) .. "::operator" .. rt.str(rt.call(v_get_operator_cpp_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "(" .. rt.str(rt.call(v_type_for_parameter,rt.arguments({rt.get(v_operator,"right_type")}),rt.dict({}))) .. "p_other) const {")}),rt.dict({}))
                        v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"other",rt.get(v_operator,"right_type"),rt._none}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"other",rt.get(v_operator,"right_type"),rt._none}),rt.dict({})),1)
                        v_result = rt.add(v_result,v_encode)
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\treturn ::godot::internal::_call_builtin_operator_ptr<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))}),rt.dict({}))) .. ">(_method_bindings.operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "_" .. rt.str(rt.get(v_operator,"right_type")) .. ", (GDExtensionConstTypePtr)&opaque, (GDExtensionConstTypePtr)" .. rt.str(v_arg_name) .. ");")}),rt.dict({}))
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))) .. " " .. rt.str(v_class_name) .. "::operator" .. rt.str(rt.call(v_get_operator_cpp_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. "() const {")}),rt.dict({}))
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\treturn ::godot::internal::_call_builtin_operator_ptr<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({rt.get(v_operator,"return_type")}),rt.dict({}))}),rt.dict({}))) .. ">(_method_bindings.operator_" .. rt.str(rt.call(v_get_operator_id_name,rt.arguments({rt.get(v_operator,"name")}),rt.dict({}))) .. ", (GDExtensionConstTypePtr)&opaque, (GDExtensionConstTypePtr) nullptr);")}),rt.dict({}))
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
                end
            end
            ::temporary_114::
        end
    end
    if rt.truth((((v_copy_constructor_index>=0)))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. " &" .. rt.str(v_class_name) .. "::operator=(const " .. rt.str(v_class_name) .. " &p_other) {")}),rt.dict({}))
        if rt.truth(rt.get(v_builtin_api,"has_destructor")) then
            rt.call(rt.attr(v_result,"append"),rt.arguments({"\t_method_bindings.destructor(&opaque);"}),rt.dict({}))
        end
        v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"other",v_class_name,rt._none}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({"other",v_class_name,rt._none}),rt.dict({})),1)
        v_result = rt.add(v_result,v_encode)
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t::godot::internal::_call_builtin_constructor(_method_bindings.constructor_" .. rt.str(v_copy_constructor_index) .. ", &opaque, " .. rt.str(v_arg_name) .. ");")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn *this;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. " &" .. rt.str(v_class_name) .. "::operator=(" .. rt.str(v_class_name) .. " &&p_other) {")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstd::swap(opaque, p_other.opaque);"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn *this;"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} //namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_result = rt.call(rt.attr(v_hooks,"alter_builtin_class_source"),rt.arguments({v_builtin_api,v_result}),rt.dict({}))
    end
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_engine_classes_bindings =
rt.fn({"api","output_dir","use_template_get_node","hooks"},{rt._missing,rt._missing,rt._missing,rt._none},function(v_api,v_output_dir,v_use_template_get_node,v_hooks)
    local v_argument,v_array_type_name,v_class_api,v_class_name,v_dict_type_name,v_dict_type_names,v_expanded_format,v_field,v_field_type,v_fully_used_classes,v_header_file,v_header_filename,v_include,v_include_gen_folder,v_included,v_includes,v_member,v_method,v_native_struct,v_result,v_singleton,v_snake_struct_name,v_source_file,v_source_filename,v_source_gen_folder,v_struct_name,v_type_name,v_used_classes
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"classes")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src"),"classes")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    for temporary_115 in rt.iter(rt.get(v_api,"classes")) do
        do
            v_class_api = temporary_115
            if rt.truth(((rt.contains(v_CLASS_ALIASES,rt.get(v_class_api,"name"))))) then
                rt.put(v_class_api,"alias_for",rt.get(v_class_api,"name"))
                rt.put(v_class_api,"name",rt.get(v_CLASS_ALIASES,rt.get(v_class_api,"alias_for")))
            end
            rt.put(v_engine_classes,rt.get(v_class_api,"name"),rt.get(v_class_api,"is_refcounted"))
        end
        ::temporary_116::
    end
    for temporary_117 in rt.iter(rt.get(v_api,"native_structures")) do
        do
            v_native_struct = temporary_117
            if rt.truth((((rt.get(v_native_struct,"name")=="ObjectID")))) then
                goto temporary_118
            end
            rt.put(v_engine_classes,rt.get(v_native_struct,"name"),false)
            rt.call(rt.attr(v_native_structures,"append"),rt.arguments({rt.get(v_native_struct,"name")}),rt.dict({}))
        end
        ::temporary_118::
    end
    for temporary_119 in rt.iter(rt.get(v_api,"singletons")) do
        do
            v_singleton = temporary_119
            if rt.truth(((rt.contains(v_CLASS_ALIASES,rt.get(v_singleton,"name"))))) then
                rt.put(v_singleton,"alias_for",rt.get(v_singleton,"name"))
                rt.put(v_singleton,"name",rt.get(v_CLASS_ALIASES,rt.get(v_singleton,"name")))
            end
            rt.call(rt.attr(v_singletons,"append"),rt.arguments({rt.get(v_singleton,"name")}),rt.dict({}))
        end
        ::temporary_120::
    end
    for temporary_121 in rt.iter(rt.get(v_api,"classes")) do
        do
            v_class_api = temporary_121
            v_used_classes = rt.call(v_set,rt.arguments({}),rt.dict({}))
            v_fully_used_classes = rt.call(v_set,rt.arguments({}),rt.dict({}))
            v_class_name = rt.get(v_class_api,"name")
            v_header_filename = rt.div(v_include_gen_folder,rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_class_api,"name")}),rt.dict({})),".hpp"))
            v_source_filename = rt.div(v_source_gen_folder,rt.add(rt.call(v_camel_to_snake,rt.arguments({rt.get(v_class_api,"name")}),rt.dict({})),".cpp"))
            if rt.truth(((rt.contains(v_class_api,"methods")))) then
                for temporary_123 in rt.iter(rt.get(v_class_api,"methods")) do
                    do
                        v_method = temporary_123
                        if rt.truth(((rt.contains(v_method,"arguments")))) then
                            for temporary_125 in rt.iter(rt.get(v_method,"arguments")) do
                                do
                                    v_argument = temporary_125
                                    v_type_name = rt.get(v_argument,"type")
                                    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                        v_type_name = rt.slice(v_type_name,6,rt._none,rt._none)
                                    end
                                    if rt.truth(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                        v_type_name = rt.slice(v_type_name,rt._none,(-1),rt._none)
                                    end
                                    if rt.truth(rt.call(v_is_included,rt.arguments({v_type_name,v_class_name}),rt.dict({}))) then
                                        if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"TypedArray"}),rt.dict({}))
                                            v_array_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typedarray::",""}),rt.dict({}))
                                            if rt.truth(rt.call(rt.attr(v_array_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                                v_array_type_name = rt.slice(v_array_type_name,6,rt._none,rt._none)
                                            end
                                            if rt.truth(rt.call(rt.attr(v_array_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                                v_array_type_name = rt.slice(v_array_type_name,rt._none,(-1),rt._none)
                                            end
                                            if rt.truth(rt.call(v_is_included,rt.arguments({v_array_type_name,v_class_name}),rt.dict({}))) then
                                                if rt.truth(rt.call(v_is_enum,rt.arguments({v_array_type_name}),rt.dict({}))) then
                                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_array_type_name}),rt.dict({}))}),rt.dict({}))
                                                else
                                                    if rt.truth(((rt.contains(v_argument,"default_value")))) then
                                                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_array_type_name}),rt.dict({}))
                                                    else
                                                        rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_array_type_name}),rt.dict({}))
                                                    end
                                                end
                                            end
                                        else
                                            if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
                                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"TypedDictionary"}),rt.dict({}))
                                                v_dict_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typeddictionary::",""}),rt.dict({}))
                                                if rt.truth(rt.call(rt.attr(v_dict_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                                    v_dict_type_name = rt.slice(v_dict_type_name,6,rt._none,rt._none)
                                                end
                                                v_dict_type_names = rt.call(rt.attr(v_dict_type_name,"split"),rt.arguments({";"}),rt.dict({}))
                                                v_dict_type_name = rt.get(v_dict_type_names,0)
                                                if rt.truth(rt.call(rt.attr(v_dict_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                                    v_dict_type_name = rt.slice(v_dict_type_name,rt._none,(-1),rt._none)
                                                end
                                                if rt.truth(rt.call(v_is_included,rt.arguments({v_dict_type_name,v_class_name}),rt.dict({}))) then
                                                    if rt.truth(rt.call(v_is_enum,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_dict_type_name}),rt.dict({}))}),rt.dict({}))
                                                    else
                                                        if rt.truth(((rt.contains(v_argument,"default_value")))) then
                                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                        else
                                                            rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                        end
                                                    end
                                                end
                                                v_dict_type_name = rt.get(v_dict_type_names,1)
                                                if rt.truth(rt.call(rt.attr(v_dict_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                                    v_dict_type_name = rt.slice(v_dict_type_name,rt._none,(-1),rt._none)
                                                end
                                                if rt.truth(rt.call(v_is_included,rt.arguments({v_dict_type_name,v_class_name}),rt.dict({}))) then
                                                    if rt.truth(rt.call(v_is_enum,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_dict_type_name}),rt.dict({}))}),rt.dict({}))
                                                    else
                                                        if rt.truth(((rt.contains(v_argument,"default_value")))) then
                                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                        else
                                                            rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                        end
                                                    end
                                                end
                                            else
                                                if rt.truth(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({}))) then
                                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_type_name}),rt.dict({}))}),rt.dict({}))
                                                else
                                                    if rt.truth(((rt.contains(v_argument,"default_value")))) then
                                                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_type_name}),rt.dict({}))
                                                    else
                                                        rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_type_name}),rt.dict({}))
                                                    end
                                                end
                                            end
                                        end
                                        if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_type_name}),rt.dict({}))) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"Ref"}),rt.dict({}))
                                        end
                                    end
                                end
                                ::temporary_126::
                            end
                        end
                        if rt.truth(((rt.contains(v_method,"return_value")))) then
                            v_type_name = rt.get(rt.get(v_method,"return_value"),"type")
                            if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                v_type_name = rt.slice(v_type_name,6,rt._none,rt._none)
                            end
                            if rt.truth(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                v_type_name = rt.slice(v_type_name,rt._none,(-1),rt._none)
                            end
                            if rt.truth(rt.call(v_is_included,rt.arguments({v_type_name,v_class_name}),rt.dict({}))) then
                                if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"TypedArray"}),rt.dict({}))
                                    v_array_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typedarray::",""}),rt.dict({}))
                                    if rt.truth(rt.call(rt.attr(v_array_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                        v_array_type_name = rt.slice(v_array_type_name,6,rt._none,rt._none)
                                    end
                                    if rt.truth(rt.call(rt.attr(v_array_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                        v_array_type_name = rt.slice(v_array_type_name,rt._none,(-1),rt._none)
                                    end
                                    if rt.truth(rt.call(v_is_included,rt.arguments({v_array_type_name,v_class_name}),rt.dict({}))) then
                                        if rt.truth(rt.call(v_is_enum,rt.arguments({v_array_type_name}),rt.dict({}))) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_array_type_name}),rt.dict({}))}),rt.dict({}))
                                        else
                                            if rt.truth(rt.call(v_is_variant,rt.arguments({v_array_type_name}),rt.dict({}))) then
                                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_array_type_name}),rt.dict({}))
                                            else
                                                rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_array_type_name}),rt.dict({}))
                                            end
                                        end
                                    end
                                else
                                    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
                                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"TypedDictionary"}),rt.dict({}))
                                        v_dict_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typeddictionary::",""}),rt.dict({}))
                                        if rt.truth(rt.call(rt.attr(v_dict_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
                                            v_dict_type_name = rt.slice(v_dict_type_name,6,rt._none,rt._none)
                                        end
                                        v_dict_type_names = rt.call(rt.attr(v_dict_type_name,"split"),rt.arguments({";"}),rt.dict({}))
                                        v_dict_type_name = rt.get(v_dict_type_names,0)
                                        if rt.truth(rt.call(rt.attr(v_dict_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                            v_dict_type_name = rt.slice(v_dict_type_name,rt._none,(-1),rt._none)
                                        end
                                        if rt.truth(rt.call(v_is_included,rt.arguments({v_dict_type_name,v_class_name}),rt.dict({}))) then
                                            if rt.truth(rt.call(v_is_enum,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_dict_type_name}),rt.dict({}))}),rt.dict({}))
                                            else
                                                if rt.truth(rt.call(v_is_variant,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                else
                                                    rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                end
                                            end
                                        end
                                        v_dict_type_name = rt.get(v_dict_type_names,1)
                                        if rt.truth(rt.call(rt.attr(v_dict_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
                                            v_dict_type_name = rt.slice(v_dict_type_name,rt._none,(-1),rt._none)
                                        end
                                        if rt.truth(rt.call(v_is_included,rt.arguments({v_dict_type_name,v_class_name}),rt.dict({}))) then
                                            if rt.truth(rt.call(v_is_enum,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_dict_type_name}),rt.dict({}))}),rt.dict({}))
                                            else
                                                if rt.truth(rt.call(v_is_variant,rt.arguments({v_dict_type_name}),rt.dict({}))) then
                                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                else
                                                    rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_dict_type_name}),rt.dict({}))
                                                end
                                            end
                                        end
                                    else
                                        if rt.truth(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({}))) then
                                            rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({v_type_name}),rt.dict({}))}),rt.dict({}))
                                        else
                                            if rt.truth(rt.call(v_is_variant,rt.arguments({v_type_name}),rt.dict({}))) then
                                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_type_name}),rt.dict({}))
                                            else
                                                rt.call(rt.attr(v_used_classes,"add"),rt.arguments({v_type_name}),rt.dict({}))
                                            end
                                        end
                                    end
                                end
                                if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_type_name}),rt.dict({}))) then
                                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"Ref"}),rt.dict({}))
                                end
                            end
                        end
                    end
                    ::temporary_124::
                end
            end
            if rt.truth(((rt.contains(v_class_api,"members")))) then
                for temporary_127 in rt.iter(rt.get(v_class_api,"members")) do
                    do
                        v_member = temporary_127
                        if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_member,"type"),v_class_name}),rt.dict({}))) then
                            if rt.truth(rt.call(v_is_enum,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) then
                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.call(v_get_enum_class,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))}),rt.dict({}))
                            else
                                rt.call(rt.attr(v_used_classes,"add"),rt.arguments({rt.get(v_member,"type")}),rt.dict({}))
                            end
                            if rt.truth(rt.call(v_is_refcounted,rt.arguments({rt.get(v_member,"type")}),rt.dict({}))) then
                                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"Ref"}),rt.dict({}))
                            end
                        end
                    end
                    ::temporary_128::
                end
            end
            if rt.truth(((rt.contains(v_class_api,"inherits")))) then
                if rt.truth(rt.call(v_is_included,rt.arguments({rt.get(v_class_api,"inherits"),v_class_name}),rt.dict({}))) then
                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({rt.get(v_class_api,"inherits")}),rt.dict({}))
                end
                if rt.truth(rt.call(v_is_refcounted,rt.arguments({rt.get(v_class_api,"name")}),rt.dict({}))) then
                    rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"Ref"}),rt.dict({}))
                end
            else
                rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({"Wrapped"}),rt.dict({}))
            end
            for temporary_129 in rt.iter(v_used_classes) do
                do
                    v_type_name = temporary_129
                    if rt.truth(rt.logical(rt.call(v_is_struct_type,rt.arguments({v_type_name}),rt.dict({})),function() return not rt.truth(rt.call(v_is_included_struct_type,rt.arguments({v_type_name}),rt.dict({}))) end,true)) then
                        rt.call(rt.attr(v_fully_used_classes,"add"),rt.arguments({v_type_name}),rt.dict({}))
                    end
                end
                ::temporary_130::
            end
            for temporary_131 in rt.iter(v_fully_used_classes) do
                do
                    v_type_name = temporary_131
                    if rt.truth(((rt.contains(v_used_classes,v_type_name)))) then
                        rt.call(rt.attr(v_used_classes,"remove"),rt.arguments({v_type_name}),rt.dict({}))
                    end
                end
                ::temporary_132::
            end
            v_used_classes = rt.call(v_list,rt.arguments({v_used_classes}),rt.dict({}))
            rt.call(rt.attr(v_used_classes,"sort"),rt.arguments({}),rt.dict({}))
            v_fully_used_classes = rt.call(v_list,rt.arguments({v_fully_used_classes}),rt.dict({}))
            if rt.truth((((rt.get(v_class_api,"name")=="RefCounted")))) then
                rt.call(rt.attr(v_fully_used_classes,"remove"),rt.arguments({"Ref"}),rt.dict({}))
            end
            rt.call(rt.attr(v_fully_used_classes,"sort"),rt.arguments({}),rt.dict({}))
            do
                local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
                rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(v_generate_engine_class_header,rt.arguments({v_class_api,v_used_classes,v_fully_used_classes,v_use_template_get_node,v_hooks}),rt.dict({}))}),rt.dict({}))
                rt.close(v_header_file)
            end
            do
                local v_source_file = rt.call(rt.attr(v_source_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
                rt.call(rt.attr(v_source_file,"write"),rt.arguments({rt.call(v_generate_engine_class_source,rt.arguments({v_class_api,v_used_classes,v_fully_used_classes,v_use_template_get_node,v_hooks}),rt.dict({}))}),rt.dict({}))
                rt.close(v_source_file)
            end
        end
        ::temporary_122::
    end
    for temporary_133 in rt.iter(rt.get(v_api,"native_structures")) do
        do
            v_native_struct = temporary_133
            v_struct_name = rt.get(v_native_struct,"name")
            if rt.truth((((v_struct_name=="ObjectID")))) then
                goto temporary_134
            end
            v_snake_struct_name = rt.call(v_camel_to_snake,rt.arguments({v_struct_name}),rt.dict({}))
            v_header_filename = rt.div(v_include_gen_folder,rt.add(v_snake_struct_name,".hpp"))
            v_result = rt.array({})
            rt.call(v_add_header,rt.arguments({(rt.str(v_snake_struct_name) .. ".hpp"),v_result}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
            v_used_classes = rt.array({})
            v_expanded_format = rt.call(rt.attr(rt.call(rt.attr(rt.call(rt.attr(rt.get(v_native_struct,"format"),"replace"),rt.arguments({"("," "}),rt.dict({})),"replace"),rt.arguments({")",";"}),rt.dict({})),"replace"),rt.arguments({",",";"}),rt.dict({}))
            for temporary_135 in rt.iter(rt.call(rt.attr(v_expanded_format,"split"),rt.arguments({";"}),rt.dict({}))) do
                do
                    v_field = temporary_135
                    v_field_type = rt.get(rt.call(rt.attr(rt.get(rt.call(rt.attr(rt.call(rt.attr(v_field,"strip"),rt.arguments({}),rt.dict({})),"split"),rt.arguments({" "}),rt.dict({})),0),"split"),rt.arguments({"::"}),rt.dict({})),0)
                    if rt.truth(rt.logical((((v_field_type~=""))),function() return rt.logical(not rt.truth(rt.call(v_is_included_type,rt.arguments({v_field_type}),rt.dict({}))),function() return not rt.truth(rt.call(v_is_pod_type,rt.arguments({v_field_type}),rt.dict({}))) end,true) end,true)) then
                        if rt.truth(((not rt.contains(v_used_classes,v_field_type)))) then
                            rt.call(rt.attr(v_used_classes,"append"),rt.arguments({v_field_type}),rt.dict({}))
                        end
                    end
                end
                ::temporary_136::
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            if rt.truth((((rt.call(v_len,rt.arguments({v_used_classes}),rt.dict({}))>0)))) then
                v_includes = rt.array({})
                for temporary_137 in rt.iter(v_used_classes) do
                    do
                        v_included = temporary_137
                        rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/" .. rt.str(rt.call(v_get_include_path,rt.arguments({v_included}),rt.dict({}))))}),rt.dict({}))
                    end
                    ::temporary_138::
                end
                rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
                for temporary_139 in rt.iter(v_includes) do
                    do
                        v_include = temporary_139
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <" .. rt.str(v_include) .. ">")}),rt.dict({}))
                    end
                    ::temporary_140::
                end
            else
                rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/method_ptrcall.hpp>"}),rt.dict({}))
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("struct " .. rt.str(v_struct_name) .. " {")}),rt.dict({}))
            for temporary_141 in rt.iter(rt.call(rt.attr(rt.get(v_native_struct,"format"),"split"),rt.arguments({";"}),rt.dict({}))) do
                do
                    v_field = temporary_141
                    if rt.truth((((v_field~="")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(v_field) .. ";")}),rt.dict({}))
                    end
                end
                ::temporary_142::
            end
            rt.call(rt.attr(v_result,"append"),rt.arguments({"};"}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("GDVIRTUAL_NATIVE_PTR(" .. rt.str(v_struct_name) .. ");")}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            do
                local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
                rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({}))}),rt.dict({}))
                rt.close(v_header_file)
            end
        end
        ::temporary_134::
    end
end)
v_generate_engine_class_header =
rt.fn({"class_api","used_classes","fully_used_classes","use_template_get_node","hooks"},{rt._missing,rt._missing,rt._missing,rt._missing,rt._missing},function(v_class_api,v_used_classes,v_fully_used_classes,v_use_template_get_node,v_hooks)
    local v_class_name,v_const_value,v_enum_api,v_include,v_included,v_includes,v_inherits,v_is_singleton,v_method,v_method_arguments,v_method_body,v_method_hash,v_method_name,v_method_signature,v_result,v_return_type,v_snake_class_name,v_type_name,v_value,v_vararg
    v_result = rt.array({})
    v_class_name = rt.get(v_class_api,"name")
    v_snake_class_name = rt.call(rt.attr(rt.call(v_camel_to_snake,rt.arguments({v_class_name}),rt.dict({})),"upper"),rt.arguments({}),rt.dict({}))
    v_is_singleton = ((rt.contains(v_singletons,v_class_name)))
    rt.call(v_add_header,rt.arguments({(rt.str(rt.call(rt.attr(v_snake_class_name,"lower"),rt.arguments({}),rt.dict({}))) .. ".hpp"),v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth((((rt.call(v_len,rt.arguments({v_fully_used_classes}),rt.dict({}))>0)))) then
        v_includes = rt.array({})
        for temporary_143 in rt.iter(v_fully_used_classes) do
            do
                v_included = temporary_143
                if rt.truth((((v_included=="TypedArray")))) then
                    rt.call(rt.attr(v_includes,"append"),rt.arguments({"godot_cpp/variant/typed_array.hpp"}),rt.dict({}))
                else
                    if rt.truth((((v_included=="TypedDictionary")))) then
                        rt.call(rt.attr(v_includes,"append"),rt.arguments({"godot_cpp/variant/typed_dictionary.hpp"}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/" .. rt.str(rt.call(v_get_include_path,rt.arguments({v_included}),rt.dict({}))))}),rt.dict({}))
                    end
                end
            end
            ::temporary_144::
        end
        rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
        for temporary_145 in rt.iter(v_includes) do
            do
                v_include = temporary_145
                rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <" .. rt.str(v_include) .. ">")}),rt.dict({}))
            end
            ::temporary_146::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="EditorPlugin")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/classes/editor_plugin_registration.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth(rt.logical((((v_class_name~="Object"))),function() return (((v_class_name~="ClassDBSingleton"))) end,true)) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/class_db.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <type_traits>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="ClassDBSingleton")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/binder_common.hpp>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Mutex")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot::CoreBind {"}),rt.dict({}))
    else
        rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    for temporary_147 in rt.iter(v_used_classes) do
        do
            v_type_name = temporary_147
            if rt.truth(rt.call(v_is_struct_type,rt.arguments({v_type_name}),rt.dict({}))) then
                rt.call(rt.attr(v_result,"append"),rt.arguments({("struct " .. rt.str(v_type_name) .. ";")}),rt.dict({}))
            else
                rt.call(rt.attr(v_result,"append"),rt.arguments({("class " .. rt.str(v_type_name) .. ";")}),rt.dict({}))
            end
        end
        ::temporary_148::
    end
    if rt.truth((((rt.call(v_len,rt.arguments({v_used_classes}),rt.dict({}))>0)))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    v_inherits = rt.choose(((rt.contains(v_class_api,"inherits"))),function() return rt.get(v_class_api,"inherits") end,function() return "Wrapped" end)
    rt.call(rt.attr(v_result,"append"),rt.arguments({("class " .. rt.str(v_class_name) .. " : public " .. rt.str(v_inherits) .. " {")}),rt.dict({}))
    if rt.truth(((rt.contains(v_class_api,"alias_for")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tGDEXTENSION_CLASS_ALIAS(" .. rt.str(v_class_name) .. ", " .. rt.str(rt.get(v_class_api,"alias_for")) .. ", " .. rt.str(v_inherits) .. ")")}),rt.dict({}))
    else
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tGDEXTENSION_CLASS(" .. rt.str(v_class_name) .. ", " .. rt.str(v_inherits) .. ")")}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_is_singleton) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic " .. rt.str(v_class_name) .. " *singleton;")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"public:"}),rt.dict({}))
    if rt.truth(((rt.contains(v_class_api,"enums")))) then
        for temporary_149 in rt.iter(rt.get(v_class_api,"enums")) do
            do
                v_enum_api = temporary_149
                if rt.truth(rt.get(v_enum_api,"is_bitfield")) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tenum " .. rt.str(rt.get(v_enum_api,"name")) .. " : uint64_t {")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tenum " .. rt.str(rt.get(v_enum_api,"name")) .. " {")}),rt.dict({}))
                end
                for temporary_151 in rt.iter(rt.get(v_enum_api,"values")) do
                    do
                        v_value = temporary_151
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t" .. rt.str(rt.get(v_value,"name")) .. " = " .. rt.str(rt.get(v_value,"value")) .. ",")}),rt.dict({}))
                    end
                    ::temporary_152::
                end
                rt.call(rt.attr(v_result,"append"),rt.arguments({"\t};"}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_150::
        end
    end
    if rt.truth(((rt.contains(v_class_api,"constants")))) then
        for temporary_153 in rt.iter(rt.get(v_class_api,"constants")) do
            do
                v_value = temporary_153
                if rt.truth(((not rt.contains(v_value,"type")))) then
                    rt.put(v_value,"type","int")
                end
                v_const_value = rt.get(v_value,"value")
                if rt.truth(rt.logical((((rt.get(v_value,"type")=="int"))),function() return (((v_const_value==(-2147483648)))) end,true)) then
                    v_const_value = "-2147483647 - 1"
                end
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic const " .. rt.str(rt.get(v_value,"type")) .. " " .. rt.str(rt.get(v_value,"name")) .. " = " .. rt.str(v_const_value) .. ";")}),rt.dict({}))
            end
            ::temporary_154::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth(v_is_singleton) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic " .. rt.str(v_class_name) .. " *get_singleton();")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_class_api,"methods")))) then
        for temporary_155 in rt.iter(rt.get(v_class_api,"methods")) do
            do
                v_method = temporary_155
                if rt.truth(rt.get(v_method,"is_virtual")) then
                    goto temporary_156
                end
                v_vararg = rt.logical(((rt.contains(v_method,"is_vararg"))),function() return rt.get(v_method,"is_vararg") end,true)
                if rt.truth(v_vararg) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"private:"}),rt.dict({}))
                end
                v_method_signature = "\t"
                v_method_signature = rt.add(v_method_signature,rt.call(v_make_signature,rt.arguments({v_class_name,v_method}),rt.dict({{"for_header",true},{"use_template_get_node",v_use_template_get_node}})))
                rt.call(rt.attr(v_result,"append"),rt.arguments({rt.add(v_method_signature,";")}),rt.dict({}))
                if rt.truth(v_vararg) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"public:"}),rt.dict({}))
                    v_result = rt.add(v_result,rt.call(v_make_varargs_template,rt.arguments({v_method}),rt.dict({})))
                end
            end
            ::temporary_156::
        end
        for temporary_157 in rt.iter(rt.get(v_class_api,"methods")) do
            do
                v_method = temporary_157
                if rt.truth(not rt.truth(rt.get(v_method,"is_virtual"))) then
                    goto temporary_158
                end
                v_method_signature = "\t"
                v_method_signature = rt.add(v_method_signature,rt.call(v_make_signature,rt.arguments({v_class_name,v_method}),rt.dict({{"for_header",true},{"use_template_get_node",v_use_template_get_node}})))
                rt.call(rt.attr(v_result,"append"),rt.arguments({rt.add(v_method_signature,";")}),rt.dict({}))
            end
            ::temporary_158::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"protected:"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename T, typename B>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic void register_virtuals() {"}),rt.dict({}))
    if rt.truth((((v_class_name~="Object")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t" .. rt.str(v_inherits) .. "::register_virtuals<T, B>();")}),rt.dict({}))
        if rt.truth(((rt.contains(v_class_api,"methods")))) then
            for temporary_159 in rt.iter(rt.get(v_class_api,"methods")) do
                do
                    v_method = temporary_159
                    if rt.truth(not rt.truth(rt.get(v_method,"is_virtual"))) then
                        goto temporary_160
                    end
                    v_method_name = rt.call(v_escape_identifier,rt.arguments({rt.get(v_method,"name")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tif constexpr (!std::is_same_v<decltype(&B::" .. rt.str(v_method_name) .. "), decltype(&T::" .. rt.str(v_method_name) .. ")>) {")}),rt.dict({}))
                    v_method_hash = rt.call(rt.attr(v_method,"get"),rt.arguments({"hash",0}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t\tBIND_VIRTUAL_METHOD(T, " .. rt.str(v_method_name) .. ", " .. rt.str(v_method_hash) .. ");")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\t}"}),rt.dict({}))
                end
                ::temporary_160::
            end
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_is_singleton) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t~" .. rt.str(v_class_name) .. "();")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Object")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString _to_string() const { return \"<\" + get_class() + \"#\" + itos(get_instance_id()) + \">\"; }"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Node")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tString _to_string() const { return (!get_name().is_empty() ? String(get_name()) + \":\" : \"\") + Object::_to_string(); }"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"public:"}),rt.dict({}))
    if rt.truth((((v_class_name=="XMLParser")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tError _open_buffer(const uint8_t *p_buffer, size_t p_size);"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Image")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tuint8_t *ptrw();"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tconst uint8_t *ptr();"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="FileAccess")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tuint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tvoid store_buffer(const uint8_t *p_src, uint64_t p_length);"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="WorkerThreadPool")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tenum {"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tINVALID_TASK_ID = -1"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t};"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttypedef int64_t TaskID;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttypedef int64_t GroupID;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tTaskID add_native_task(void (*p_func)(void *), void *p_userdata, bool p_high_priority = false, const String &p_description = String());"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tGroupID add_native_group_task(void (*p_func)(void *, uint32_t), void *p_userdata, int p_elements, int p_tasks = -1, bool p_high_priority = false, const String &p_description = String());"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="SceneTree")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic SceneTree *get_singleton();"}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Object")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename T>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic T *cast_to(Object *p_object);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename T>"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tstatic const T *cast_to(const Object *p_object);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tvirtual ~Object() = default;"}),rt.dict({}))
    else
        if rt.truth(rt.logical(v_use_template_get_node,function() return (((v_class_name=="Node"))) end,true)) then
            rt.call(rt.attr(v_result,"append"),rt.arguments({"\ttemplate <typename T>"}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({"\tT *get_node(const NodePath &p_path) const { return Object::cast_to<T>(get_node_internal(p_path)); }"}),rt.dict({}))
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"};"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(rt.logical(((rt.contains(v_class_api,"enums"))),function() return (((v_class_name~="Object"))) end,true)) then
        for temporary_161 in rt.iter(rt.get(v_class_api,"enums")) do
            do
                v_enum_api = temporary_161
                if rt.truth(rt.get(v_enum_api,"is_bitfield")) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("VARIANT_BITFIELD_CAST(" .. rt.str(v_class_name) .. "::" .. rt.str(rt.get(v_enum_api,"name")) .. ");")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("VARIANT_ENUM_CAST(" .. rt.str(v_class_name) .. "::" .. rt.str(rt.get(v_enum_api,"name")) .. ");")}),rt.dict({}))
                end
            end
            ::temporary_162::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="ClassDBSingleton")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#define CLASSDB_SINGLETON_FORWARD_METHODS \\"}),rt.dict({}))
        if rt.truth(((rt.contains(v_class_api,"enums")))) then
            for temporary_163 in rt.iter(rt.get(v_class_api,"enums")) do
                do
                    v_enum_api = temporary_163
                    if rt.truth(rt.get(v_enum_api,"is_bitfield")) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tenum " .. rt.str(rt.get(v_enum_api,"name")) .. " : uint64_t { \\")}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tenum " .. rt.str(rt.get(v_enum_api,"name")) .. " { \\")}),rt.dict({}))
                    end
                    for temporary_165 in rt.iter(rt.get(v_enum_api,"values")) do
                        do
                            v_value = temporary_165
                            rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t" .. rt.str(rt.get(v_value,"name")) .. " = " .. rt.str(rt.get(v_value,"value")) .. ", \\")}),rt.dict({}))
                        end
                        ::temporary_166::
                    end
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}; \\"}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t \\"}),rt.dict({}))
                end
                ::temporary_164::
            end
        end
        for temporary_167 in rt.iter(rt.get(v_class_api,"methods")) do
            do
                v_method = temporary_167
                if rt.truth(rt.logical(((rt.contains(v_method,"is_static"))),function() return rt.get(v_method,"is_static") end,true)) then
                    goto temporary_168
                end
                v_vararg = rt.logical(((rt.contains(v_method,"is_vararg"))),function() return rt.get(v_method,"is_vararg") end,true)
                if rt.truth(v_vararg) then
                    v_method_signature = "\ttemplate <typename... Args> static "
                else
                    v_method_signature = "\tstatic "
                end
                v_return_type = rt._none
                if rt.truth(((rt.contains(v_method,"return_type")))) then
                    v_return_type = rt.call(v_correct_type,rt.arguments({rt.call(rt.attr(rt.get(v_method,"return_type"),"replace"),rt.arguments({"ClassDBSingleton","ClassDB"}),rt.dict({})),rt._none,false}),rt.dict({}))
                else
                    if rt.truth(((rt.contains(v_method,"return_value")))) then
                        v_return_type = rt.call(v_correct_type,rt.arguments({rt.call(rt.attr(rt.get(rt.get(v_method,"return_value"),"type"),"replace"),rt.arguments({"ClassDBSingleton","ClassDB"}),rt.dict({})),rt.call(rt.attr(rt.get(v_method,"return_value"),"get"),rt.arguments({"meta",rt._none}),rt.dict({})),false}),rt.dict({}))
                    end
                end
                if rt.truth(((not rt.same(v_return_type,rt._none)))) then
                    v_method_signature = rt.add(v_method_signature,v_return_type)
                    if rt.truth(not rt.truth(rt.call(rt.attr(v_method_signature,"endswith"),rt.arguments({"*"}),rt.dict({})))) then
                        v_method_signature = rt.add(v_method_signature," ")
                    end
                else
                    v_method_signature = rt.add(v_method_signature,"void ")
                end
                v_method_signature = rt.add(v_method_signature,(rt.str(rt.get(v_method,"name")) .. "("))
                v_method_arguments = rt.array({})
                if rt.truth(((rt.contains(v_method,"arguments")))) then
                    v_method_arguments = rt.get(v_method,"arguments")
                end
                v_method_signature = rt.add(v_method_signature,rt.call(v_make_function_parameters,rt.arguments({v_method_arguments}),rt.dict({{"include_default",true},{"for_builtin",true},{"is_vararg",v_vararg}})))
                v_method_signature = rt.add(v_method_signature,") { \\")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
                v_method_body = "\t\t"
                if rt.truth(((not rt.same(v_return_type,rt._none)))) then
                    v_method_body = rt.add(v_method_body,"return ")
                    if rt.truth(rt.logical(((rt.contains(v_class_api,"alias_for"))),function() return rt.call(rt.attr(v_return_type,"startswith"),rt.arguments({rt.add(rt.get(v_class_api,"alias_for"),"::")}),rt.dict({})) end,true)) then
                        v_method_body = rt.add(v_method_body,("(" .. rt.str(v_return_type) .. ")"))
                    end
                end
                v_method_body = rt.add(v_method_body,("ClassDBSingleton::get_singleton()->" .. rt.str(rt.get(v_method,"name")) .. "("))
                v_method_body = rt.add(v_method_body,rt.call(rt.attr(", ","join"),rt.arguments({rt.call(v_map,rt.arguments({rt.fn({"x"},{rt._missing},function(v_x)
    do return rt.call(v_escape_argument,rt.arguments({rt.get(v_x,"name")}),rt.dict({})) end
end),v_method_arguments}),rt.dict({}))}),rt.dict({})))
                if rt.truth(v_vararg) then
                    v_method_body = rt.add(v_method_body,", p_args...")
                end
                v_method_body = rt.add(v_method_body,"); \\")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_body}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({"\t} \\"}),rt.dict({}))
            end
            ::temporary_168::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#define CLASSDB_SINGLETON_VARIANT_CAST \\"}),rt.dict({}))
        if rt.truth(((rt.contains(v_class_api,"enums")))) then
            for temporary_169 in rt.iter(rt.get(v_class_api,"enums")) do
                do
                    v_enum_api = temporary_169
                    if rt.truth(rt.get(v_enum_api,"is_bitfield")) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tVARIANT_BITFIELD_CAST(" .. rt.str(rt.get(v_class_api,"alias_for")) .. "::" .. rt.str(rt.get(v_enum_api,"name")) .. "); \\")}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tVARIANT_ENUM_CAST(" .. rt.str(rt.get(v_class_api,"alias_for")) .. "::" .. rt.str(rt.get(v_enum_api,"name")) .. "); \\")}),rt.dict({}))
                    end
                end
                ::temporary_170::
            end
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_result = rt.call(rt.attr(v_hooks,"alter_engine_class_header"),rt.arguments({v_class_api,v_result}),rt.dict({}))
    end
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_engine_class_source =
rt.fn({"class_api","used_classes","fully_used_classes","use_template_get_node","hooks"},{rt._missing,rt._missing,rt._missing,rt._missing,rt._none},function(v_class_api,v_used_classes,v_fully_used_classes,v_use_template_get_node,v_hooks)
    local v_arg_name,v_argument,v_arguments,v_class_name,v_encode,v_has_return,v_included,v_includes,v_is_ref,v_is_singleton,v_meta_type,v_method,v_method_call,v_method_signature,v_result,v_return_type,v_snake_class_name,v_vararg
    v_result = rt.array({})
    v_class_name = rt.get(v_class_api,"name")
    v_snake_class_name = rt.call(v_camel_to_snake,rt.arguments({v_class_name}),rt.dict({}))
    v_is_singleton = ((rt.contains(v_singletons,v_class_name)))
    rt.call(v_add_header,rt.arguments({(rt.str(v_snake_class_name) .. ".cpp"),v_result}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <godot_cpp/classes/" .. rt.str(v_snake_class_name) .. ".hpp>")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/class_db.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/engine_ptrcall.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"#include <godot_cpp/core/error_macros.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth((((rt.call(v_len,rt.arguments({v_used_classes}),rt.dict({}))>0)))) then
        v_includes = rt.array({})
        for temporary_171 in rt.iter(v_used_classes) do
            do
                v_included = temporary_171
                rt.call(rt.attr(v_includes,"append"),rt.arguments({("godot_cpp/" .. rt.str(rt.call(v_get_include_path,rt.arguments({v_included}),rt.dict({}))))}),rt.dict({}))
            end
            ::temporary_172::
        end
        rt.call(rt.attr(v_includes,"sort"),rt.arguments({}),rt.dict({}))
        for temporary_173 in rt.iter(v_includes) do
            do
                v_included = temporary_173
                rt.call(rt.attr(v_result,"append"),rt.arguments({("#include <" .. rt.str(v_included) .. ">")}),rt.dict({}))
            end
            ::temporary_174::
        end
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth((((v_class_name=="Mutex")))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot::CoreBind {"}),rt.dict({}))
    else
        rt.call(rt.attr(v_result,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_is_singleton) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. " *" .. rt.str(v_class_name) .. "::singleton = nullptr;")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. " *" .. rt.str(v_class_name) .. "::get_singleton() {")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tif (unlikely(singleton == nullptr)) {"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tGDExtensionObjectPtr singleton_obj = ::godot::gdextension_interface::global_get_singleton(" .. rt.str(v_class_name) .. "::get_class_static()._native_ptr());")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#ifdef DEBUG_ENABLED"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tERR_FAIL_NULL_V(singleton_obj, nullptr);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif // DEBUG_ENABLED"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tsingleton = reinterpret_cast<" .. rt.str(v_class_name) .. " *>(::godot::gdextension_interface::object_get_instance_binding(singleton_obj, ::godot::gdextension_interface::token, &" .. rt.str(v_class_name) .. "::_gde_binding_callbacks));")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#ifdef DEBUG_ENABLED"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tERR_FAIL_NULL_V(singleton, nullptr);"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"#endif // DEBUG_ENABLED"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tif (likely(singleton)) {"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\t\tClassDB::_register_engine_singleton(" .. rt.str(v_class_name) .. "::get_class_static(), singleton);")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\t}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn singleton;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({(rt.str(v_class_name) .. "::~" .. rt.str(v_class_name) .. "() {")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\tif (singleton == this) {"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t\tClassDB::_unregister_engine_singleton(" .. rt.str(v_class_name) .. "::get_class_static());")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tsingleton = nullptr;"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_class_api,"methods")))) then
        for temporary_175 in rt.iter(rt.get(v_class_api,"methods")) do
            do
                v_method = temporary_175
                if rt.truth(rt.get(v_method,"is_virtual")) then
                    goto temporary_176
                end
                v_vararg = rt.logical(((rt.contains(v_method,"is_vararg"))),function() return rt.get(v_method,"is_vararg") end,true)
                v_method_signature = rt.call(v_make_signature,rt.arguments({v_class_name,v_method}),rt.dict({{"use_template_get_node",v_use_template_get_node}}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({rt.add(v_method_signature," {")}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstatic GDExtensionMethodBindPtr _gde_method_bind = ::godot::gdextension_interface::classdb_get_method_bind(" .. rt.str(v_class_name) .. "::get_class_static()._native_ptr(), StringName(\"" .. rt.str(rt.get(v_method,"name")) .. "\")._native_ptr(), " .. rt.str(rt.get(v_method,"hash")) .. ");")}),rt.dict({}))
                v_method_call = "\t"
                v_has_return = rt.logical(((rt.contains(v_method,"return_value"))),function() return (((rt.get(rt.get(v_method,"return_value"),"type")~="void"))) end,true)
                if rt.truth(v_has_return) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tCHECK_METHOD_BIND_RET(_gde_method_bind, (" .. rt.str(rt.call(v_get_default_value_for_type,rt.arguments({rt.get(rt.get(v_method,"return_value"),"type")}),rt.dict({}))) .. "));")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tCHECK_METHOD_BIND(_gde_method_bind);"}),rt.dict({}))
                end
                v_is_ref = false
                if rt.truth(not rt.truth(v_vararg)) then
                    if rt.truth(v_has_return) then
                        v_return_type = rt.get(rt.get(v_method,"return_value"),"type")
                        v_meta_type = rt.choose(((rt.contains(rt.get(v_method,"return_value"),"meta"))),function() return rt.get(rt.get(v_method,"return_value"),"meta") end,function() return rt._none end)
                        if rt.truth(rt.call(v_is_enum,rt.arguments({v_return_type}),rt.dict({}))) then
                            if rt.truth(rt.get(v_method,"is_static")) then
                                v_method_call = rt.add(v_method_call,("return (" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type,v_meta_type}),rt.dict({}))}),rt.dict({}))) .. ")::godot::internal::_call_native_mb_ret<int64_t>(_gde_method_bind, nullptr"))
                            else
                                v_method_call = rt.add(v_method_call,("return (" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type,v_meta_type}),rt.dict({}))}),rt.dict({}))) .. ")::godot::internal::_call_native_mb_ret<int64_t>(_gde_method_bind, _owner"))
                            end
                        else
                            if rt.truth(rt.logical(rt.call(v_is_pod_type,rt.arguments({v_return_type}),rt.dict({})),function() return rt.call(v_is_variant,rt.arguments({v_return_type}),rt.dict({})) end,false)) then
                                if rt.truth(rt.get(v_method,"is_static")) then
                                    v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_native_mb_ret<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type,v_meta_type}),rt.dict({}))}),rt.dict({}))) .. ">(_gde_method_bind, nullptr"))
                                else
                                    v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_native_mb_ret<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({v_return_type,v_meta_type}),rt.dict({}))}),rt.dict({}))) .. ">(_gde_method_bind, _owner"))
                                end
                            else
                                if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_return_type}),rt.dict({}))) then
                                    if rt.truth(rt.get(v_method,"is_static")) then
                                        v_method_call = rt.add(v_method_call,("return Ref<" .. rt.str(v_return_type) .. ">::_gde_internal_constructor(::godot::internal::_call_native_mb_ret_obj<" .. rt.str(v_return_type) .. ">(_gde_method_bind, nullptr"))
                                    else
                                        v_method_call = rt.add(v_method_call,("return Ref<" .. rt.str(v_return_type) .. ">::_gde_internal_constructor(::godot::internal::_call_native_mb_ret_obj<" .. rt.str(v_return_type) .. ">(_gde_method_bind, _owner"))
                                    end
                                    v_is_ref = true
                                else
                                    if rt.truth(rt.get(v_method,"is_static")) then
                                        v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_native_mb_ret_obj<" .. rt.str(v_return_type) .. ">(_gde_method_bind, nullptr"))
                                    else
                                        v_method_call = rt.add(v_method_call,("return ::godot::internal::_call_native_mb_ret_obj<" .. rt.str(v_return_type) .. ">(_gde_method_bind, _owner"))
                                    end
                                end
                            end
                        end
                    else
                        if rt.truth(rt.get(v_method,"is_static")) then
                            v_method_call = rt.add(v_method_call,"::godot::internal::_call_native_mb_no_ret(_gde_method_bind, nullptr")
                        else
                            v_method_call = rt.add(v_method_call,"::godot::internal::_call_native_mb_no_ret(_gde_method_bind, _owner")
                        end
                    end
                    if rt.truth(((rt.contains(v_method,"arguments")))) then
                        v_method_call = rt.add(v_method_call,", ")
                        v_arguments = rt.array({})
                        for temporary_177 in rt.iter(rt.get(v_method,"arguments")) do
                            do
                                v_argument = temporary_177
                                v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),1)
                                v_result = rt.add(v_result,v_encode)
                                rt.call(rt.attr(v_arguments,"append"),rt.arguments({v_arg_name}),rt.dict({}))
                            end
                            ::temporary_178::
                        end
                        v_method_call = rt.add(v_method_call,rt.call(rt.attr(", ","join"),rt.arguments({v_arguments}),rt.dict({})))
                    end
                else
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tGDExtensionCallError error;"}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tVariant ret;"}),rt.dict({}))
                    v_method_call = rt.add(v_method_call,"::godot::gdextension_interface::object_method_bind_call(_gde_method_bind, _owner, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count, &ret, &error")
                end
                if rt.truth(v_is_ref) then
                    v_method_call = rt.add(v_method_call,")")
                end
                v_method_call = rt.add(v_method_call,");")
                rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_call}),rt.dict({}))
                if rt.truth(rt.logical(v_vararg,function() return rt.logical(((rt.contains(v_method,"return_value"))),function() return (((rt.get(rt.get(v_method,"return_value"),"type")~="void"))) end,true) end,true)) then
                    v_return_type = rt.call(v_get_enum_fullname,rt.arguments({rt.get(rt.get(v_method,"return_value"),"type")}),rt.dict({}))
                    if rt.truth((((v_return_type~="Variant")))) then
                        rt.call(rt.attr(v_result,"append"),rt.arguments({("\treturn VariantCaster<" .. rt.str(v_return_type) .. ">::cast(ret);")}),rt.dict({}))
                    else
                        rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn ret;"}),rt.dict({}))
                    end
                end
                rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_176::
        end
        for temporary_179 in rt.iter(rt.get(v_class_api,"methods")) do
            do
                v_method = temporary_179
                if rt.truth(not rt.truth(rt.get(v_method,"is_virtual"))) then
                    goto temporary_180
                end
                v_method_signature = rt.call(v_make_signature,rt.arguments({v_class_name,v_method}),rt.dict({{"use_template_get_node",v_use_template_get_node}}))
                v_method_signature = rt.add(v_method_signature," {")
                if rt.truth(rt.logical(((rt.contains(v_method,"return_value"))),function() return (((rt.call(v_correct_type,rt.arguments({rt.get(rt.get(v_method,"return_value"),"type")}),rt.dict({}))~="void"))) end,true)) then
                    rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({("\treturn " .. rt.str(rt.call(v_get_default_value_for_type,rt.arguments({rt.get(rt.get(v_method,"return_value"),"type")}),rt.dict({}))) .. ";")}),rt.dict({}))
                    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
                else
                    v_method_signature = rt.add(v_method_signature,"}")
                    rt.call(rt.attr(v_result,"append"),rt.arguments({v_method_signature}),rt.dict({}))
                end
                rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
            end
            ::temporary_180::
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_result = rt.call(rt.attr(v_hooks,"alter_engine_class_source"),rt.arguments({v_class_api,v_result}),rt.dict({}))
    end
    do return rt.call(rt.attr("\n","join"),rt.arguments({v_result}),rt.dict({})) end
end)
v_generate_global_constants =
rt.fn({"api","output_dir","hooks"},{rt._missing,rt._missing,rt._none},function(v_api,v_output_dir,v_hooks)
    local v_c,v_constant,v_enum_def,v_global_constants,v_header,v_header_file,v_header_filename,v_include_gen_folder,v_limit_constants,v_source_gen_folder,v_value
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"classes")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src"),"classes")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_header = rt.array({})
    rt.call(v_add_header,rt.arguments({"global_constants.hpp",v_header}),rt.dict({}))
    v_header_filename = rt.div(v_include_gen_folder,"global_constants.hpp")
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#include <cstdint>"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    v_limit_constants = rt.set(rt.array({"UINT8_MAX","UINT16_MAX","UINT32_MAX","INT8_MIN","INT8_MAX","INT16_MIN","INT16_MAX","INT32_MIN","INT32_MAX","INT64_MIN","INT64_MAX"}))
    v_global_constants = (function() local temporary_181=rt.array({}); for temporary_182 in rt.iter(rt.get(v_api,"global_constants")) do v_c = temporary_182; if rt.truth(((not rt.contains(v_limit_constants,rt.get(v_c,"name"))))) then rt.append(temporary_181,v_c) end end; return temporary_181 end)()
    if rt.truth((((rt.call(v_len,rt.arguments({v_global_constants}),rt.dict({}))>0)))) then
        for temporary_183 in rt.iter(v_global_constants) do
            do
                v_constant = temporary_183
                rt.call(rt.attr(v_header,"append"),rt.arguments({("const int64_t " .. rt.str(rt.call(v_escape_identifier,rt.arguments({rt.get(v_constant,"name")}),rt.dict({}))) .. " = " .. rt.str(rt.get(v_constant,"value")) .. ";")}),rt.dict({}))
            end
            ::temporary_184::
        end
        rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    end
    for temporary_185 in rt.iter(rt.get(v_api,"global_enums")) do
        do
            v_enum_def = temporary_185
            if rt.truth(rt.call(rt.attr(rt.get(v_enum_def,"name"),"startswith"),rt.arguments({"Variant."}),rt.dict({}))) then
                goto temporary_186
            end
            if rt.truth(rt.get(v_enum_def,"is_bitfield")) then
                rt.call(rt.attr(v_header,"append"),rt.arguments({("enum " .. rt.str(rt.get(v_enum_def,"name")) .. " : uint64_t {")}),rt.dict({}))
            else
                rt.call(rt.attr(v_header,"append"),rt.arguments({("enum " .. rt.str(rt.get(v_enum_def,"name")) .. " {")}),rt.dict({}))
            end
            for temporary_187 in rt.iter(rt.get(v_enum_def,"values")) do
                do
                    v_value = temporary_187
                    rt.call(rt.attr(v_header,"append"),rt.arguments({("\t" .. rt.str(rt.get(v_value,"name")) .. " = " .. rt.str(rt.get(v_value,"value")) .. ",")}),rt.dict({}))
                end
                ::temporary_188::
            end
            rt.call(rt.attr(v_header,"append"),rt.arguments({"};"}),rt.dict({}))
            rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
        end
        ::temporary_186::
    end
    rt.call(rt.attr(v_header,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_header = rt.call(rt.attr(v_hooks,"alter_global_constants"),rt.arguments({v_api,v_header}),rt.dict({}))
    end
    do
        local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_header}),rt.dict({}))}),rt.dict({}))
        rt.close(v_header_file)
    end
end)
v_generate_version_header =
rt.fn({"api","output_dir"},{rt._missing,rt._missing},function(v_api,v_output_dir)
    local v_header,v_header_file,v_header_file_path,v_header_filename,v_include_gen_folder
    v_header = rt.array({})
    v_header_filename = "version.hpp"
    rt.call(v_add_header,rt.arguments({v_header_filename,v_header}),rt.dict({}))
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"core")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_header_file_path = rt.div(v_include_gen_folder,v_header_filename)
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({("#define GODOT_VERSION_MAJOR " .. rt.str(rt.get(rt.get(v_api,"header"),"version_major")))}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({("#define GODOT_VERSION_MINOR " .. rt.str(rt.get(rt.get(v_api,"header"),"version_minor")))}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({("#define GODOT_VERSION_PATCH " .. rt.str(rt.get(rt.get(v_api,"header"),"version_patch")))}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({("#define GODOT_VERSION_STATUS \"" .. rt.str(rt.get(rt.get(v_api,"header"),"version_status")) .. "\"")}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({("#define GODOT_VERSION_BUILD \"" .. rt.str(rt.get(rt.get(v_api,"header"),"version_build")) .. "\"")}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    do
        local v_header_file = rt.call(rt.attr(v_header_file_path,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_header}),rt.dict({}))}),rt.dict({}))
        rt.close(v_header_file)
    end
end)
v_generate_global_constant_binds =
rt.fn({"api","output_dir"},{rt._missing,rt._missing},function(v_api,v_output_dir)
    local v_enum_def,v_header,v_header_file,v_header_filename,v_include_gen_folder,v_source_gen_folder
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"classes")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src"),"classes")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_header = rt.array({})
    rt.call(v_add_header,rt.arguments({"global_constants_binds.hpp",v_header}),rt.dict({}))
    v_header_filename = rt.div(v_include_gen_folder,"global_constants_binds.hpp")
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#include <godot_cpp/classes/global_constants.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    for temporary_189 in rt.iter(rt.get(v_api,"global_enums")) do
        do
            v_enum_def = temporary_189
            if rt.truth(rt.call(rt.attr(rt.get(v_enum_def,"name"),"startswith"),rt.arguments({"Variant."}),rt.dict({}))) then
                goto temporary_190
            end
            if rt.truth(rt.get(v_enum_def,"is_bitfield")) then
                rt.call(rt.attr(v_header,"append"),rt.arguments({("VARIANT_BITFIELD_CAST(" .. rt.str(rt.get(v_enum_def,"name")) .. ");")}),rt.dict({}))
            else
                rt.call(rt.attr(v_header,"append"),rt.arguments({("VARIANT_ENUM_CAST(" .. rt.str(rt.get(v_enum_def,"name")) .. ");")}),rt.dict({}))
            end
        end
        ::temporary_190::
    end
    rt.call(rt.attr(v_header,"append"),rt.arguments({"VARIANT_ENUM_CAST(godot::Variant::Type);"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    do
        local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_header}),rt.dict({}))}),rt.dict({}))
        rt.close(v_header_file)
    end
end)
v_generate_utility_functions =
rt.fn({"api","output_dir","hooks"},{rt._missing,rt._missing,rt._none},function(v_api,v_output_dir,v_hooks)
    local v_arg_name,v_argument,v_arguments,v_encode,v_function,v_function_call,v_function_signature,v_has_return,v_header,v_header_file,v_header_filename,v_include_gen_folder,v_source,v_source_file,v_source_filename,v_source_gen_folder,v_vararg
    v_include_gen_folder = rt.div(rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"include"),"godot_cpp"),"variant")
    v_source_gen_folder = rt.div(rt.div(rt.call(v_Path,rt.arguments({v_output_dir}),rt.dict({})),"src"),"variant")
    rt.call(rt.attr(v_include_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    rt.call(rt.attr(v_source_gen_folder,"mkdir"),rt.arguments({}),rt.dict({{"parents",true},{"exist_ok",true}}))
    v_header = rt.array({})
    rt.call(v_add_header,rt.arguments({"utility_functions.hpp",v_header}),rt.dict({}))
    v_header_filename = rt.div(v_include_gen_folder,"utility_functions.hpp")
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#pragma once"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#include <godot_cpp/variant/builtin_types.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#include <godot_cpp/variant/variant.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"#include <array>"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"class UtilityFunctions {"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"public:"}),rt.dict({}))
    for temporary_191 in rt.iter(rt.get(v_api,"utility_functions")) do
        do
            v_function = temporary_191
            if rt.truth((((rt.get(v_function,"name")=="is_instance_valid")))) then
                goto temporary_192
            end
            v_vararg = rt.logical(((rt.contains(v_function,"is_vararg"))),function() return rt.get(v_function,"is_vararg") end,true)
            if rt.truth(v_vararg) then
                rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
                rt.call(rt.attr(v_header,"append"),rt.arguments({"private:"}),rt.dict({}))
            end
            v_function_signature = "\t"
            v_function_signature = rt.add(v_function_signature,rt.call(v_make_signature,rt.arguments({"UtilityFunctions",v_function}),rt.dict({{"for_header",true},{"static",true}})))
            rt.call(rt.attr(v_header,"append"),rt.arguments({rt.add(v_function_signature,";")}),rt.dict({}))
            if rt.truth(v_vararg) then
                rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
                rt.call(rt.attr(v_header,"append"),rt.arguments({"public:"}),rt.dict({}))
                v_header = rt.add(v_header,rt.call(v_make_varargs_template,rt.arguments({v_function}),rt.dict({{"static",true}})))
            end
        end
        ::temporary_192::
    end
    rt.call(rt.attr(v_header,"append"),rt.arguments({"};"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_header,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_header = rt.call(rt.attr(v_hooks,"alter_utility_functions_header"),rt.arguments({v_api,v_header}),rt.dict({}))
    end
    do
        local v_header_file = rt.call(rt.attr(v_header_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_header_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_header}),rt.dict({}))}),rt.dict({}))
        rt.close(v_header_file)
    end
    v_source = rt.array({})
    rt.call(v_add_header,rt.arguments({"utility_functions.cpp",v_source}),rt.dict({}))
    v_source_filename = rt.div(v_source_gen_folder,"utility_functions.cpp")
    rt.call(rt.attr(v_source,"append"),rt.arguments({"#include <godot_cpp/variant/utility_functions.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({"#include <godot_cpp/core/engine_ptrcall.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({"#include <godot_cpp/core/error_macros.hpp>"}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({""}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({"namespace godot {"}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({""}),rt.dict({}))
    for temporary_193 in rt.iter(rt.get(v_api,"utility_functions")) do
        do
            v_function = temporary_193
            if rt.truth((((rt.get(v_function,"name")=="is_instance_valid")))) then
                goto temporary_194
            end
            v_vararg = rt.logical(((rt.contains(v_function,"is_vararg"))),function() return rt.get(v_function,"is_vararg") end,true)
            v_function_signature = rt.call(v_make_signature,rt.arguments({"UtilityFunctions",v_function}),rt.dict({}))
            rt.call(rt.attr(v_source,"append"),rt.arguments({rt.add(v_function_signature," {")}),rt.dict({}))
            rt.call(rt.attr(v_source,"append"),rt.arguments({("\tstatic GDExtensionPtrUtilityFunction _gde_function = ::godot::gdextension_interface::variant_get_ptr_utility_function(StringName(\"" .. rt.str(rt.get(v_function,"name")) .. "\")._native_ptr(), " .. rt.str(rt.get(v_function,"hash")) .. ");")}),rt.dict({}))
            v_has_return = rt.logical(((rt.contains(v_function,"return_type"))),function() return (((rt.get(v_function,"return_type")~="void"))) end,true)
            if rt.truth(v_has_return) then
                rt.call(rt.attr(v_source,"append"),rt.arguments({("\tCHECK_METHOD_BIND_RET(_gde_function, (" .. rt.str(rt.call(v_get_default_value_for_type,rt.arguments({rt.get(v_function,"return_type")}),rt.dict({}))) .. "));")}),rt.dict({}))
            else
                rt.call(rt.attr(v_source,"append"),rt.arguments({"\tCHECK_METHOD_BIND(_gde_function);"}),rt.dict({}))
            end
            v_function_call = "\t"
            if rt.truth(not rt.truth(v_vararg)) then
                if rt.truth(v_has_return) then
                    v_function_call = rt.add(v_function_call,"return ")
                    if rt.truth((((rt.get(v_function,"return_type")=="Object")))) then
                        v_function_call = rt.add(v_function_call,"::godot::internal::_call_utility_ret_obj(_gde_function")
                    else
                        v_function_call = rt.add(v_function_call,("::godot::internal::_call_utility_ret<" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({rt.get(v_function,"return_type")}),rt.dict({}))}),rt.dict({}))) .. ">(_gde_function"))
                    end
                else
                    v_function_call = rt.add(v_function_call,"::godot::internal::_call_utility_no_ret(_gde_function")
                end
                if rt.truth(((rt.contains(v_function,"arguments")))) then
                    v_function_call = rt.add(v_function_call,", ")
                    v_arguments = rt.array({})
                    for temporary_195 in rt.iter(rt.get(v_function,"arguments")) do
                        do
                            v_argument = temporary_195
                            v_encode = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),0); v_arg_name = rt.get(rt.call(v_get_encoded_arg,rt.arguments({rt.get(v_argument,"name"),rt.get(v_argument,"type"),rt.choose(((rt.contains(v_argument,"meta"))),function() return rt.get(v_argument,"meta") end,function() return rt._none end)}),rt.dict({})),1)
                            v_source = rt.add(v_source,v_encode)
                            rt.call(rt.attr(v_arguments,"append"),rt.arguments({v_arg_name}),rt.dict({}))
                        end
                        ::temporary_196::
                    end
                    v_function_call = rt.add(v_function_call,rt.call(rt.attr(", ","join"),rt.arguments({v_arguments}),rt.dict({})))
                end
            else
                if rt.truth(v_has_return) then
                    rt.call(rt.attr(v_source,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({rt.call(v_correct_type,rt.arguments({rt.get(v_function,"return_type")}),rt.dict({}))}),rt.dict({}))) .. " ret;")}),rt.dict({}))
                else
                    rt.call(rt.attr(v_source,"append"),rt.arguments({"\tVariant ret;"}),rt.dict({}))
                end
                v_function_call = rt.add(v_function_call,"_gde_function(&ret, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count")
            end
            v_function_call = rt.add(v_function_call,");")
            rt.call(rt.attr(v_source,"append"),rt.arguments({v_function_call}),rt.dict({}))
            if rt.truth(rt.logical(v_vararg,function() return v_has_return end,true)) then
                rt.call(rt.attr(v_source,"append"),rt.arguments({"\treturn ret;"}),rt.dict({}))
            end
            rt.call(rt.attr(v_source,"append"),rt.arguments({"}"}),rt.dict({}))
            rt.call(rt.attr(v_source,"append"),rt.arguments({""}),rt.dict({}))
        end
        ::temporary_194::
    end
    rt.call(rt.attr(v_source,"append"),rt.arguments({"} // namespace godot"}),rt.dict({}))
    rt.call(rt.attr(v_source,"append"),rt.arguments({""}),rt.dict({}))
    if rt.truth(v_hooks) then
        v_header = rt.call(rt.attr(v_hooks,"alter_utility_functions_source"),rt.arguments({v_api,v_source}),rt.dict({}))
    end
    do
        local v_source_file = rt.call(rt.attr(v_source_filename,"open"),rt.arguments({"w+"}),rt.dict({{"encoding","utf-8"}}))
        rt.call(rt.attr(v_source_file,"write"),rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_source}),rt.dict({}))}),rt.dict({}))
        rt.close(v_source_file)
    end
end)
v_camel_to_snake =
rt.fn({"name"},{rt._missing},function(v_name)
    v_name = rt.call(rt.attr(v_re,"sub"),rt.arguments({"(.)([A-Z][a-z]+)","\\1_\\2",v_name}),rt.dict({}))
    v_name = rt.call(rt.attr(v_re,"sub"),rt.arguments({"([a-z0-9])([A-Z])","\\1_\\2",v_name}),rt.dict({}))
    do return rt.call(rt.attr(rt.call(rt.attr(rt.call(rt.attr(v_name,"replace"),rt.arguments({"2_D","2D"}),rt.dict({})),"replace"),rt.arguments({"3_D","3D"}),rt.dict({})),"lower"),rt.arguments({}),rt.dict({})) end
end)
v_make_function_parameters =
rt.fn({"parameters","include_default","for_builtin","is_vararg"},{rt._missing,false,false,false},function(v_parameters,v_include_default,v_for_builtin,v_is_vararg)
    local v_index,v_par,v_parameter,v_parameter_name,v_parameter_type,v_signature
    v_signature = rt.array({})
    for temporary_197 in rt.iter(rt.call(v_enumerate,rt.arguments({v_parameters}),rt.dict({}))) do
        do
            v_index = rt.get(temporary_197,0); v_par = rt.get(temporary_197,1)
            v_parameter = rt.call(v_type_for_parameter,rt.arguments({rt.get(v_par,"type"),rt.choose(((rt.contains(v_par,"meta"))),function() return rt.get(v_par,"meta") end,function() return rt._none end)}),rt.dict({}))
            v_parameter_name = rt.call(v_escape_argument,rt.arguments({rt.get(v_par,"name")}),rt.dict({}))
            if rt.truth((((rt.call(v_len,rt.arguments({v_parameter_name}),rt.dict({}))==0)))) then
                v_parameter_name = rt.add("p_arg_",rt.call(v_str,rt.arguments({rt.add(v_index,1)}),rt.dict({})))
            end
            v_parameter = rt.add(v_parameter,v_parameter_name)
            if rt.truth(rt.logical(v_include_default,function() return rt.logical(((rt.contains(v_par,"default_value"))),function() return rt.logical(not rt.truth(v_for_builtin),function() return (((rt.get(v_par,"type")~="Variant"))) end,false) end,true) end,true)) then
                v_parameter = rt.add(v_parameter," = ")
                if rt.truth(rt.call(v_is_enum,rt.arguments({rt.get(v_par,"type")}),rt.dict({}))) then
                    v_parameter_type = rt.call(v_correct_type,rt.arguments({rt.get(v_par,"type")}),rt.dict({}))
                    if rt.truth((((v_parameter_type=="void")))) then
                        v_parameter_type = "Variant"
                    end
                    v_parameter = rt.add(v_parameter,("(" .. rt.str(v_parameter_type) .. ")"))
                end
                v_parameter = rt.add(v_parameter,rt.call(v_correct_default_value,rt.arguments({rt.get(v_par,"default_value"),rt.get(v_par,"type")}),rt.dict({})))
            end
            rt.call(rt.attr(v_signature,"append"),rt.arguments({v_parameter}),rt.dict({}))
        end
        ::temporary_198::
    end
    if rt.truth(v_is_vararg) then
        rt.call(rt.attr(v_signature,"append"),rt.arguments({"const Args &...p_args"}),rt.dict({}))
    end
    do return rt.call(rt.attr(", ","join"),rt.arguments({v_signature}),rt.dict({})) end
end)
v_type_for_parameter =
rt.fn({"type_name","meta"},{rt._missing,rt._none},function(v_type_name,v_meta)
    if rt.truth((((v_type_name=="void")))) then
        do return "Variant " end
    else
        if rt.truth(rt.logical(rt.logical(rt.call(v_is_pod_type,rt.arguments({v_type_name}),rt.dict({})),function() return (((v_type_name~="Nil"))) end,true),function() return rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({})) end,false)) then
            do return (rt.str(rt.call(v_correct_type,rt.arguments({v_type_name,v_meta}),rt.dict({}))) .. " ") end
        else
            if rt.truth(rt.logical(rt.call(v_is_variant,rt.arguments({v_type_name}),rt.dict({})),function() return rt.call(v_is_refcounted,rt.arguments({v_type_name}),rt.dict({})) end,false)) then
                do return ("const " .. rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))) .. " &") end
            else
                do return (rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({})))) end
            end
        end
    end
end)
v_get_include_path =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    local v_base_dir
    v_base_dir = ""
    if rt.truth((((v_type_name=="Object")))) then
        v_base_dir = "core"
    else
        if rt.truth(rt.call(v_is_variant,rt.arguments({v_type_name}),rt.dict({}))) then
            v_base_dir = "variant"
        else
            v_base_dir = "classes"
        end
    end
    do return (rt.str(v_base_dir) .. "/" .. rt.str(rt.call(v_camel_to_snake,rt.arguments({v_type_name}),rt.dict({}))) .. ".hpp") end
end)
v_get_encoded_arg =
rt.fn({"arg_name","type_name","type_meta"},{rt._missing,rt._missing,rt._missing},function(v_arg_name,v_type_name,v_type_meta)
    local v_arg_type,v_name,v_result
    v_result = rt.array({})
    v_name = rt.call(v_escape_argument,rt.arguments({v_arg_name}),rt.dict({}))
    v_arg_type = rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))
    if rt.truth(rt.call(v_is_pod_type,rt.arguments({v_arg_type}),rt.dict({}))) then
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_get_gdextension_type,rt.arguments({v_arg_type}),rt.dict({}))) .. " " .. rt.str(v_name) .. "_encoded;")}),rt.dict({}))
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\tPtrToArg<" .. rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))) .. ">::encode(" .. rt.str(v_name) .. ", &" .. rt.str(v_name) .. "_encoded);")}),rt.dict({}))
        v_name = ("&" .. rt.str(v_name) .. "_encoded")
    else
        if rt.truth(rt.logical(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({})),function() return not rt.truth(rt.call(v_is_bitfield,rt.arguments({v_type_name}),rt.dict({}))) end,true)) then
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\tint64_t " .. rt.str(v_name) .. "_encoded;")}),rt.dict({}))
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\tPtrToArg<int64_t>::encode(" .. rt.str(v_name) .. ", &" .. rt.str(v_name) .. "_encoded);")}),rt.dict({}))
            v_name = ("&" .. rt.str(v_name) .. "_encoded")
        else
            if rt.truth(rt.call(v_is_engine_class,rt.arguments({v_type_name}),rt.dict({}))) then
                v_name = ("(" .. rt.str(v_name) .. " != nullptr ? &" .. rt.str(v_name) .. "->_owner : nullptr)")
            else
                v_name = ("&" .. rt.str(v_name))
            end
        end
    end
    do return rt.array({v_result,v_name}) end
end)
v_make_signature =
rt.fn({"class_name","function_data","for_header","use_template_get_node","for_builtin","static"},{rt._missing,rt._missing,false,true,false,false},function(v_class_name,v_function_data,v_for_header,v_use_template_get_node,v_for_builtin,v_static)
    local v_arguments,v_function_signature,v_is_vararg,v_return_meta,v_return_type
    v_function_signature = ""
    v_is_vararg = rt.logical(((rt.contains(v_function_data,"is_vararg"))),function() return rt.get(v_function_data,"is_vararg") end,true)
    if rt.truth(v_for_header) then
        if rt.truth(rt.logical(((rt.contains(v_function_data,"is_virtual"))),function() return rt.get(v_function_data,"is_virtual") end,true)) then
            v_function_signature = rt.add(v_function_signature,"virtual ")
        end
        if rt.truth(v_static) then
            v_function_signature = rt.add(v_function_signature,"static ")
        end
    end
    v_return_type = "void"
    v_return_meta = rt._none
    if rt.truth(((rt.contains(v_function_data,"return_type")))) then
        v_return_type = rt.call(v_correct_type,rt.arguments({rt.get(v_function_data,"return_type")}),rt.dict({}))
    else
        if rt.truth(((rt.contains(v_function_data,"return_value")))) then
            v_return_type = rt.get(rt.get(v_function_data,"return_value"),"type")
            v_return_meta = rt.choose(((rt.contains(rt.get(v_function_data,"return_value"),"meta"))),function() return rt.get(rt.get(v_function_data,"return_value"),"meta") end,function() return rt._none end)
        end
    end
    v_function_signature = rt.add(v_function_signature,rt.call(v_correct_type,rt.arguments({v_return_type,v_return_meta}),rt.dict({})))
    if rt.truth(not rt.truth(rt.call(rt.attr(v_function_signature,"endswith"),rt.arguments({"*"}),rt.dict({})))) then
        v_function_signature = rt.add(v_function_signature," ")
    end
    if rt.truth(not rt.truth(v_for_header)) then
        v_function_signature = rt.add(v_function_signature,(rt.str(v_class_name) .. "::"))
    end
    v_function_signature = rt.add(v_function_signature,rt.call(v_escape_identifier,rt.arguments({rt.get(v_function_data,"name")}),rt.dict({})))
    if rt.truth(rt.logical(v_is_vararg,function() return rt.logical(not rt.truth(v_for_builtin),function() return rt.logical(v_use_template_get_node,function() return rt.logical((((v_class_name=="Node"))),function() return (((rt.get(v_function_data,"name")=="get_node"))) end,true) end,true) end,true) end,false)) then
        v_function_signature = rt.add(v_function_signature,"_internal")
    end
    v_function_signature = rt.add(v_function_signature,"(")
    v_arguments = rt.choose(((rt.contains(v_function_data,"arguments"))),function() return rt.get(v_function_data,"arguments") end,function() return rt.array({}) end)
    if rt.truth(not rt.truth(v_is_vararg)) then
        v_function_signature = rt.add(v_function_signature,rt.call(v_make_function_parameters,rt.arguments({v_arguments,v_for_header,v_for_builtin,v_is_vararg}),rt.dict({})))
    else
        v_function_signature = rt.add(v_function_signature,"const Variant **p_args, GDExtensionInt p_arg_count")
    end
    v_function_signature = rt.add(v_function_signature,")")
    if rt.truth(rt.logical(((rt.contains(v_function_data,"is_static"))),function() return rt.logical(rt.get(v_function_data,"is_static"),function() return v_for_header end,true) end,true)) then
        v_function_signature = rt.add("static ",v_function_signature)
    else
        if rt.truth(rt.logical(((rt.contains(v_function_data,"is_const"))),function() return rt.get(v_function_data,"is_const") end,true)) then
            v_function_signature = rt.add(v_function_signature," const")
        end
    end
    do return v_function_signature end
end)
v_make_varargs_template =
rt.fn({"function_data","static","class_befor_signature","with_indent","for_builtin_classes"},{rt._missing,false,"",true,false},function(v_function_data,v_static,v_class_befor_signature,v_with_indent,v_for_builtin_classes)
    local v_args_array,v_argument,v_base,v_call_line,v_function_name,v_function_signature,v_i,v_is_vararg,v_method_arguments,v_result,v_ret,v_return_meta,v_return_type
    v_result = rt.array({})
    v_function_signature = ""
    rt.call(rt.attr(v_result,"append"),rt.arguments({"template <typename... Args>"}),rt.dict({}))
    if rt.truth(v_static) then
        v_function_signature = rt.add(v_function_signature,"static ")
    end
    v_return_type = "void"
    v_return_meta = rt._none
    if rt.truth(((rt.contains(v_function_data,"return_type")))) then
        v_return_type = rt.call(v_correct_type,rt.arguments({rt.get(v_function_data,"return_type")}),rt.dict({}))
    else
        if rt.truth(((rt.contains(v_function_data,"return_value")))) then
            v_return_type = rt.get(rt.get(v_function_data,"return_value"),"type")
            v_return_meta = rt.choose(((rt.contains(rt.get(v_function_data,"return_value"),"meta"))),function() return rt.get(rt.get(v_function_data,"return_value"),"meta") end,function() return rt._none end)
        end
    end
    v_function_signature = rt.add(v_function_signature,rt.call(v_correct_type,rt.arguments({v_return_type,v_return_meta}),rt.dict({})))
    if rt.truth(not rt.truth(rt.call(rt.attr(v_function_signature,"endswith"),rt.arguments({"*"}),rt.dict({})))) then
        v_function_signature = rt.add(v_function_signature," ")
    end
    if rt.truth((((rt.call(v_len,rt.arguments({v_class_befor_signature}),rt.dict({}))>0)))) then
        v_function_signature = rt.add(v_function_signature,rt.add(v_class_befor_signature,"::"))
    end
    v_function_signature = rt.add(v_function_signature,(rt.str(rt.call(v_escape_identifier,rt.arguments({rt.get(v_function_data,"name")}),rt.dict({})))))
    v_method_arguments = rt.array({})
    if rt.truth(((rt.contains(v_function_data,"arguments")))) then
        v_method_arguments = rt.get(v_function_data,"arguments")
    end
    v_function_signature = rt.add(v_function_signature,"(")
    v_is_vararg = rt.logical(((rt.contains(v_function_data,"is_vararg"))),function() return rt.get(v_function_data,"is_vararg") end,true)
    v_function_signature = rt.add(v_function_signature,rt.call(v_make_function_parameters,rt.arguments({v_method_arguments}),rt.dict({{"include_default",true},{"is_vararg",v_is_vararg}})))
    v_function_signature = rt.add(v_function_signature,")")
    if rt.truth(rt.logical(((rt.contains(v_function_data,"is_const"))),function() return rt.get(v_function_data,"is_const") end,true)) then
        v_function_signature = rt.add(v_function_signature," const")
    end
    v_function_signature = rt.add(v_function_signature," {")
    rt.call(rt.attr(v_result,"append"),rt.arguments({v_function_signature}),rt.dict({}))
    v_args_array = ("\tstd::array<Variant, " .. rt.str(rt.call(v_len,rt.arguments({v_method_arguments}),rt.dict({}))) .. " + sizeof...(Args)> variant_args{{ ")
    for temporary_199 in rt.iter(v_method_arguments) do
        do
            v_argument = temporary_199
            if rt.truth((((rt.get(v_argument,"type")=="Variant")))) then
                v_args_array = rt.add(v_args_array,rt.call(v_escape_argument,rt.arguments({rt.get(v_argument,"name")}),rt.dict({})))
            else
                v_args_array = rt.add(v_args_array,("Variant(" .. rt.str(rt.call(v_escape_argument,rt.arguments({rt.get(v_argument,"name")}),rt.dict({}))) .. ")"))
            end
            v_args_array = rt.add(v_args_array,", ")
        end
        ::temporary_200::
    end
    v_args_array = rt.add(v_args_array,"Variant(p_args)... }};")
    rt.call(rt.attr(v_result,"append"),rt.arguments({v_args_array}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({("\tstd::array<const Variant *, " .. rt.str(rt.call(v_len,rt.arguments({v_method_arguments}),rt.dict({}))) .. " + sizeof...(Args)> call_args;")}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\tfor (size_t i = 0; i < variant_args.size(); i++) {"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t\tcall_args[i] = &variant_args[i];"}),rt.dict({}))
    rt.call(rt.attr(v_result,"append"),rt.arguments({"\t}"}),rt.dict({}))
    v_call_line = "\t"
    if rt.truth(not rt.truth(v_for_builtin_classes)) then
        if rt.truth((((v_return_type~="void")))) then
            v_call_line = rt.add(v_call_line,"return ")
        end
        v_call_line = rt.add(v_call_line,(rt.str(rt.call(v_escape_identifier,rt.arguments({rt.get(v_function_data,"name")}),rt.dict({}))) .. "_internal(call_args.data(), variant_args.size());"))
        rt.call(rt.attr(v_result,"append"),rt.arguments({v_call_line}),rt.dict({}))
    else
        v_base = "(GDExtensionTypePtr)&opaque"
        if rt.truth(v_static) then
            v_base = "nullptr"
        end
        v_ret = "nullptr"
        if rt.truth((((v_return_type~="void")))) then
            v_ret = "&ret"
            rt.call(rt.attr(v_result,"append"),rt.arguments({("\t" .. rt.str(rt.call(v_correct_type,rt.arguments({rt.get(v_function_data,"return_type")}),rt.dict({}))) .. " ret;")}),rt.dict({}))
        end
        v_function_name = rt.get(v_function_data,"name")
        rt.call(rt.attr(v_result,"append"),rt.arguments({("\t_method_bindings.method_" .. rt.str(v_function_name) .. "(" .. rt.str(v_base) .. ", reinterpret_cast<GDExtensionConstTypePtr *>(call_args.data()), " .. rt.str(v_ret) .. ", " .. rt.str(rt.call(v_len,rt.arguments({v_method_arguments}),rt.dict({}))) .. " + sizeof...(Args));")}),rt.dict({}))
        if rt.truth((((v_return_type~="void")))) then
            rt.call(rt.attr(v_result,"append"),rt.arguments({"\treturn ret;"}),rt.dict({}))
        end
    end
    rt.call(rt.attr(v_result,"append"),rt.arguments({"}"}),rt.dict({}))
    if rt.truth(v_with_indent) then
        for temporary_201 in rt.iter(rt.call(v_range,rt.arguments({rt.call(v_len,rt.arguments({v_result}),rt.dict({}))}),rt.dict({}))) do
            do
                v_i = temporary_201
                rt.put(v_result,v_i,rt.add("\t",rt.get(v_result,v_i)))
            end
            ::temporary_202::
        end
    end
    do return v_result end
end)
v_is_pod_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return ((rt.contains(rt.array({"Nil","void","bool","real_t","float","double","int","int8_t","uint8_t","int16_t","uint16_t","int32_t","int64_t","uint32_t","uint64_t"}),v_type_name))) end
end)
v_is_included_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical(rt.call(v_is_included_struct_type,rt.arguments({v_type_name}),rt.dict({})),function() return ((rt.contains(rt.array({"ObjectID"}),v_type_name))) end,false) end
end)
v_is_included_struct_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return ((rt.contains(rt.array({"AABB","Basis","Color","Plane","Projection","Quaternion","Rect2","Rect2i","Transform2D","Transform3D","Vector2","Vector2i","Vector3","Vector3i","Vector4","Vector4i"}),v_type_name))) end
end)
v_is_packed_array =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return ((rt.contains(rt.array({"PackedByteArray","PackedColorArray","PackedFloat32Array","PackedFloat64Array","PackedInt32Array","PackedInt64Array","PackedStringArray","PackedVector2Array","PackedVector3Array","PackedVector4Array"}),v_type_name))) end
end)
v_needs_copy_instead_of_move =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return ((rt.contains(rt.array({"Dictionary"}),v_type_name))) end
end)
v_is_enum =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"enum::"}),rt.dict({})),function() return rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"bitfield::"}),rt.dict({})) end,false) end
end)
v_is_bitfield =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"bitfield::"}),rt.dict({})) end
end)
v_get_enum_class =
rt.fn({"enum_name"},{rt._missing},function(v_enum_name)
    if rt.truth(((rt.contains(v_enum_name,".")))) then
        if rt.truth(rt.call(v_is_bitfield,rt.arguments({v_enum_name}),rt.dict({}))) then
            do return rt.get(rt.call(rt.attr(rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"bitfield::",""}),rt.dict({})),"split"),rt.arguments({"."}),rt.dict({})),0) end
        else
            do return rt.get(rt.call(rt.attr(rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"enum::",""}),rt.dict({})),"split"),rt.arguments({"."}),rt.dict({})),0) end
        end
    else
        do return "GlobalConstants" end
    end
end)
v_get_enum_fullname =
rt.fn({"enum_name"},{rt._missing},function(v_enum_name)
    if rt.truth(rt.call(v_is_bitfield,rt.arguments({v_enum_name}),rt.dict({}))) then
        do return rt.add(rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"bitfield::","BitField<"}),rt.dict({})),">") end
    else
        do return rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"enum::",""}),rt.dict({})) end
    end
end)
v_get_enum_name =
rt.fn({"enum_name"},{rt._missing},function(v_enum_name)
    if rt.truth(rt.call(v_is_bitfield,rt.arguments({v_enum_name}),rt.dict({}))) then
        do return rt.get(rt.call(rt.attr(rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"bitfield::",""}),rt.dict({})),"split"),rt.arguments({"."}),rt.dict({})),(-1)) end
    else
        do return rt.get(rt.call(rt.attr(rt.call(rt.attr(v_enum_name,"replace"),rt.arguments({"enum::",""}),rt.dict({})),"split"),rt.arguments({"."}),rt.dict({})),(-1)) end
    end
end)
v_is_variant =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical((((v_type_name=="Variant"))),function() return rt.logical(((rt.contains(v_builtin_classes,v_type_name))),function() return rt.logical((((v_type_name=="Nil"))),function() return rt.logical(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({})),function() return rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({})) end,false) end,false) end,false) end,false) end
end)
v_is_engine_class =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical((((v_type_name=="Object"))),function() return ((rt.contains(v_engine_classes,v_type_name))) end,false) end
end)
v_is_struct_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical(rt.call(v_is_included_struct_type,rt.arguments({v_type_name}),rt.dict({})),function() return ((rt.contains(v_native_structures,v_type_name))) end,false) end
end)
v_is_refcounted =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    do return rt.logical(((rt.contains(v_engine_classes,v_type_name))),function() return rt.get(v_engine_classes,v_type_name) end,true) end
end)
v_is_included =
rt.fn({"type_name","current_type"},{rt._missing,rt._missing},function(v_type_name,v_current_type)
    local v_to_include
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
        do return true end
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
        do return true end
    end
    v_to_include = rt.choose(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({})),function() return rt.call(v_get_enum_class,rt.arguments({v_type_name}),rt.dict({})) end,function() return v_type_name end)
    if rt.truth(rt.logical((((v_to_include==v_current_type))),function() return rt.call(v_is_pod_type,rt.arguments({v_to_include}),rt.dict({})) end,false)) then
        do return false end
    end
    if rt.truth(rt.logical((((v_to_include=="GlobalConstants"))),function() return (((v_to_include=="UtilityFunctions"))) end,false)) then
        do return true end
    end
    do return rt.logical(rt.call(v_is_engine_class,rt.arguments({v_to_include}),rt.dict({})),function() return rt.call(v_is_variant,rt.arguments({v_to_include}),rt.dict({})) end,false) end
end)
v_correct_default_value =
rt.fn({"value","type_name"},{rt._missing,rt._missing},function(v_value,v_type_name)
    local v_value_map
    v_value_map = rt.dict({{"inf","Math::INF"},{"nan","Math::NaN"},{"null","nullptr"},{"\"\"","String()"},{"&\"\"","StringName()"},{"^\"\"","NodePath()"},{"[]","Array()"},{"{}","Dictionary()"},{"Transform2D(1, 0, 0, 1, 0, 0)","Transform2D()"},{"Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)","Transform3D()"}})
    if rt.truth(((rt.contains(v_value_map,v_value)))) then
        do return rt.get(v_value_map,v_value) end
    end
    if rt.truth((((v_value=="")))) then
        do return (rt.str(v_type_name) .. "()") end
    end
    if rt.truth(rt.call(rt.attr(v_value,"startswith"),rt.arguments({"Array["}),rt.dict({}))) then
        do return "{}" end
    end
    if rt.truth(rt.call(rt.attr(v_value,"startswith"),rt.arguments({"&"}),rt.dict({}))) then
        do return rt.slice(v_value,1,rt._none,rt._none) end
    end
    if rt.truth(rt.call(rt.attr(v_value,"startswith"),rt.arguments({"^"}),rt.dict({}))) then
        do return rt.slice(v_value,1,rt._none,rt._none) end
    end
    do return v_value end
end)
v_correct_typed_array =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
        do return rt.add(rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typedarray::","TypedArray<"}),rt.dict({})),">") end
    end
    do return v_type_name end
end)
v_correct_typed_dictionary =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
        do return rt.add(rt.call(rt.attr(rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typeddictionary::","TypedDictionary<"}),rt.dict({})),"replace"),rt.arguments({";",", "}),rt.dict({})),">") end
    end
    do return v_type_name end
end)
v_correct_type =
rt.fn({"type_name","meta","use_alias"},{rt._missing,rt._none,true},function(v_type_name,v_meta,v_use_alias)
    local v_arr_type_name,v_base_class,v_dict_type_name,v_dict_type_names,v_key_name,v_type_conversion,v_val_name
    v_type_conversion = rt.dict({{"float","double"},{"int","int64_t"},{"Nil","Variant"}})
    if rt.truth(((not rt.same(v_meta,rt._none)))) then
        if rt.truth(((rt.contains(rt.array({"int8","int16","int32","int64","uint8","uint16","uint32","uint64"}),v_meta)))) then
            do return (rt.str(v_meta) .. "_t") end
        else
            if rt.truth(((rt.contains(rt.array({"float","double"}),v_meta)))) then
                do return v_meta end
            else
                if rt.truth(((rt.contains(rt.array({"char16","char32"}),v_meta)))) then
                    do return (rt.str(v_meta) .. "_t") end
                end
            end
        end
    end
    if rt.truth(((rt.contains(v_type_conversion,v_type_name)))) then
        do return rt.get(v_type_conversion,v_type_name) end
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
        v_arr_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typedarray::",""}),rt.dict({}))
        if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_arr_type_name}),rt.dict({}))) then
            v_arr_type_name = rt.add(rt.add("Ref<",v_arr_type_name),">")
        end
        do return rt.add(rt.add("TypedArray<",v_arr_type_name),">") end
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
        v_dict_type_name = rt.call(rt.attr(v_type_name,"replace"),rt.arguments({"typeddictionary::",""}),rt.dict({}))
        v_dict_type_names = rt.call(rt.attr(v_dict_type_name,"split"),rt.arguments({";"}),rt.dict({}))
        if rt.truth(rt.call(v_is_refcounted,rt.arguments({rt.get(v_dict_type_names,0)}),rt.dict({}))) then
            v_key_name = rt.add(rt.add("Ref<",rt.get(v_dict_type_names,0)),">")
        else
            v_key_name = rt.get(v_dict_type_names,0)
        end
        if rt.truth(rt.call(v_is_refcounted,rt.arguments({rt.get(v_dict_type_names,1)}),rt.dict({}))) then
            v_val_name = rt.add(rt.add("Ref<",rt.get(v_dict_type_names,1)),">")
        else
            v_val_name = rt.get(v_dict_type_names,1)
        end
        do return rt.add(rt.add(rt.add(rt.add("TypedDictionary<",v_key_name),", "),v_val_name),">") end
    end
    if rt.truth(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({}))) then
        if rt.truth(rt.call(v_is_bitfield,rt.arguments({v_type_name}),rt.dict({}))) then
            v_base_class = rt.call(v_get_enum_class,rt.arguments({v_type_name}),rt.dict({}))
            if rt.truth(rt.logical(v_use_alias,function() return ((rt.contains(v_CLASS_ALIASES,v_base_class))) end,true)) then
                v_base_class = rt.get(v_CLASS_ALIASES,v_base_class)
            end
            if rt.truth((((v_base_class=="GlobalConstants")))) then
                do return ("BitField<" .. rt.str(rt.call(v_get_enum_name,rt.arguments({v_type_name}),rt.dict({}))) .. ">") end
            end
            do return ("BitField<" .. rt.str(v_base_class) .. "::" .. rt.str(rt.call(v_get_enum_name,rt.arguments({v_type_name}),rt.dict({}))) .. ">") end
        else
            v_base_class = rt.call(v_get_enum_class,rt.arguments({v_type_name}),rt.dict({}))
            if rt.truth(rt.logical(v_use_alias,function() return ((rt.contains(v_CLASS_ALIASES,v_base_class))) end,true)) then
                v_base_class = rt.get(v_CLASS_ALIASES,v_base_class)
            end
            if rt.truth((((v_base_class=="GlobalConstants")))) then
                do return (rt.str(rt.call(v_get_enum_name,rt.arguments({v_type_name}),rt.dict({})))) end
            end
            do return (rt.str(v_base_class) .. "::" .. rt.str(rt.call(v_get_enum_name,rt.arguments({v_type_name}),rt.dict({})))) end
        end
    end
    if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_type_name}),rt.dict({}))) then
        do return ("Ref<" .. rt.str(v_type_name) .. ">") end
    end
    if rt.truth(rt.logical((((v_type_name=="Object"))),function() return rt.call(v_is_engine_class,rt.arguments({v_type_name}),rt.dict({})) end,false)) then
        do return (rt.str(v_type_name) .. " *") end
    end
    if rt.truth(rt.logical(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({"*"}),rt.dict({})),function() return rt.logical(not rt.truth(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({"**"}),rt.dict({}))),function() return not rt.truth(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({" *"}),rt.dict({}))) end,true) end,true)) then
        do return (rt.str(rt.slice(v_type_name,rt._none,(-1),rt._none)) .. " *") end
    end
    do return v_type_name end
end)
v_get_gdextension_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    local v_type_conversion_map
    v_type_conversion_map = rt.dict({{"bool","int8_t"},{"uint8_t","int64_t"},{"int8_t","int64_t"},{"uint16_t","int64_t"},{"int16_t","int64_t"},{"uint32_t","int64_t"},{"int32_t","int64_t"},{"int","int64_t"},{"float","double"}})
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"BitField<"}),rt.dict({}))) then
        do return "int64_t" end
    end
    if rt.truth(((rt.contains(v_type_conversion_map,v_type_name)))) then
        do return rt.get(v_type_conversion_map,v_type_name) end
    end
    do return v_type_name end
end)
v_escape_identifier =
rt.fn({"id"},{rt._missing},function(v_id)
    local v_cpp_keywords_map
    v_cpp_keywords_map = rt.dict({{"class","_class"},{"char","_char"},{"short","_short"},{"bool","_bool"},{"int","_int"},{"default","_default"},{"case","_case"},{"switch","_switch"},{"export","_export"},{"template","_template"},{"new","new_"},{"operator","_operator"},{"typeof","type_of"},{"typename","type_name"},{"enum","_enum"}})
    if rt.truth(((rt.contains(v_cpp_keywords_map,v_id)))) then
        do return rt.get(v_cpp_keywords_map,v_id) end
    end
    do return v_id end
end)
v_escape_argument =
rt.fn({"id"},{rt._missing},function(v_id)
    if rt.truth(rt.logical(rt.call(rt.attr(v_id,"startswith"),rt.arguments({"p_"}),rt.dict({})),function() return rt.call(rt.attr(v_id,"startswith"),rt.arguments({"r_"}),rt.dict({})) end,false)) then
        do return v_id end
    end
    do return rt.add("p_",v_id) end
end)
v_get_operator_id_name =
rt.fn({"op"},{rt._missing},function(v_op)
    local v_op_id_map
    v_op_id_map = rt.dict({{"==","equal"},{"!=","not_equal"},{"<","less"},{"<=","less_equal"},{">","greater"},{">=","greater_equal"},{"+","add"},{"-","subtract"},{"*","multiply"},{"/","divide"},{"unary-","negate"},{"unary+","positive"},{"%","module"},{"**","power"},{"<<","shift_left"},{">>","shift_right"},{"&","bit_and"},{"|","bit_or"},{"^","bit_xor"},{"~","bit_negate"},{"and","and"},{"or","or"},{"xor","xor"},{"not","not"},{"in","in"}})
    do return rt.get(v_op_id_map,v_op) end
end)
v_get_operator_cpp_name =
rt.fn({"op"},{rt._missing},function(v_op)
    local v_op_cpp_map
    v_op_cpp_map = rt.dict({{"==","=="},{"!=","!="},{"<","<"},{"<=","<="},{">",">"},{">=",">="},{"+","+"},{"-","-"},{"*","*"},{"/","/"},{"unary-","-"},{"unary+","+"},{"%","%"},{"<<","<<"},{">>",">>"},{"&","&"},{"|","|"},{"^","^"},{"~","~"},{"and","&&"},{"or","||"},{"not","!"}})
    do return rt.get(v_op_cpp_map,v_op) end
end)
v_is_valid_cpp_operator =
rt.fn({"op"},{rt._missing},function(v_op)
    do return ((not rt.contains(rt.array({"**","xor","in"}),v_op))) end
end)
v_get_default_value_for_type =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    if rt.truth((((v_type_name=="int")))) then
        do return "0" end
    end
    if rt.truth((((v_type_name=="float")))) then
        do return "0.0" end
    end
    if rt.truth((((v_type_name=="bool")))) then
        do return "false" end
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typedarray::"}),rt.dict({}))) then
        do return (rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))) .. "()") end
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"typeddictionary::"}),rt.dict({}))) then
        do return (rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))) .. "()") end
    end
    if rt.truth(rt.call(v_is_enum,rt.arguments({v_type_name}),rt.dict({}))) then
        do return (rt.str(rt.call(v_correct_type,rt.arguments({v_type_name}),rt.dict({}))) .. "(0)") end
    end
    if rt.truth(rt.call(v_is_variant,rt.arguments({v_type_name}),rt.dict({}))) then
        do return (rt.str(v_type_name) .. "()") end
    end
    if rt.truth(rt.call(v_is_refcounted,rt.arguments({v_type_name}),rt.dict({}))) then
        do return ("Ref<" .. rt.str(v_type_name) .. ">()") end
    end
    do return "nullptr" end
end)
v_header = "/**************************************************************************/\n/*  $filename                                                             */\n/**************************************************************************/\n/*                         This file is part of:                          */\n/*                             GODOT ENGINE                               */\n/*                        https://godotengine.org                         */\n/**************************************************************************/\n/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */\n/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */\n/*                                                                        */\n/* Permission is hereby granted, free of charge, to any person obtaining  */\n/* a copy of this software and associated documentation files (the        */\n/* \"Software\"), to deal in the Software without restriction, including    */\n/* without limitation the rights to use, copy, modify, merge, publish,    */\n/* distribute, sublicense, and/or sell copies of the Software, and to     */\n/* permit persons to whom the Software is furnished to do so, subject to  */\n/* the following conditions:                                              */\n/*                                                                        */\n/* The above copyright notice and this permission notice shall be         */\n/* included in all copies or substantial portions of the Software.        */\n/*                                                                        */\n/* THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,        */\n/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */\n/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */\n/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */\n/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */\n/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */\n/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */\n/**************************************************************************/\n"
v_add_header =
rt.fn({"filename","lines"},{rt._missing,rt._missing},function(v_filename,v_lines)
    local v_desired_length,v_line,v_new_line,v_num,v_pad_spaces
    v_desired_length = rt.call(v_len,rt.arguments({rt.get(rt.call(rt.attr(v_header,"split"),rt.arguments({"\n"}),rt.dict({})),0)}),rt.dict({}))
    v_pad_spaces = rt.sub(rt.sub(v_desired_length,6),rt.call(v_len,rt.arguments({v_filename}),rt.dict({})))
    for temporary_203 in rt.iter(rt.call(v_enumerate,rt.arguments({rt.call(rt.attr(v_header,"split"),rt.arguments({"\n"}),rt.dict({}))}),rt.dict({}))) do
        do
            v_num = rt.get(temporary_203,0); v_line = rt.get(temporary_203,1)
            if rt.truth((((v_num==1)))) then
                v_new_line = ("/*  " .. rt.str(v_filename) .. rt.str(rt.mul(" ",v_pad_spaces)) .. "*/")
                rt.call(rt.attr(v_lines,"append"),rt.arguments({v_new_line}),rt.dict({}))
            else
                rt.call(rt.attr(v_lines,"append"),rt.arguments({v_line}),rt.dict({}))
            end
        end
        ::temporary_204::
    end
    rt.call(rt.attr(v_lines,"append"),rt.arguments({"// THIS FILE IS GENERATED. EDITS WILL BE LOST."}),rt.dict({}))
    rt.call(rt.attr(v_lines,"append"),rt.arguments({""}),rt.dict({}))
end)
function main(api, interface, output, bits, precision)
    v_builtin_classes = rt.array({}); v_engine_classes = rt.dict({}); v_native_structures = rt.array({}); v_singletons = rt.array({})
    return rt.call(v_generate_bindings,rt.array({api,interface,true,bits or "64",precision or "single",output}),rt.dict({}))
end
