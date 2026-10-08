-- Native Lua SDK emitter, ported from the pinned godot-cpp algorithm. See its MIT LICENSE.md.
import("build.xmake.generators.sdk_runtime", {rootdir = os.projectdir()})
local rt = sdk_runtime.main()
local v_dict,v_difflib = rt.dict,rt.difflib
local v__get_buffer,v__generated_wrapper,v_generate_gdextension_interface_header,v_check_formatting,v_check_allowed_keys,v_base_type_name,v_format_type_and_name,v_is_valid_type,v_check_type,v_write_doc,v_make_deprecated_message,v_make_deprecated_comment_for_type,v_write_simple_type,v_write_enum_type,v_make_args_text,v_write_function_type,v_write_struct_type,v_write_interface,v_BASE_TYPES
local v_len,v_str,v_print,v_open,v_range,v_enumerate,v_sorted,v_set,v_list,v_map,v_Exception,v_Path,v_isinstance = rt.len,rt.str,print,rt.open,rt.range,rt.enumerate,rt.sorted,rt.set,rt.list,rt.map,rt.str,rt.Path,rt.isinstance
local v_json,v_shutil,v_re,v_OrderedDict,v_UnknownTypeError = rt.json,rt.shutil,rt.re,rt.dict,rt.unknown_type
v_BASE_TYPES = rt.array({"void","int","int8_t","uint8_t","int16_t","uint16_t","int32_t","uint32_t","int64_t","uint64_t","size_t","char","char16_t","char32_t","wchar_t","float","double"})
v__get_buffer =
rt.fn({"path"},{rt._missing},function(v_path)
    local v_file
    do
        local v_file = rt.call(v_open,rt.arguments({v_path,"rb"}),rt.dict({}))
        local contents = rt.call(rt.attr(v_file,"read"),rt.arguments({}),rt.dict({}))
        rt.close(v_file)
        do return contents end
    end
end)
v__generated_wrapper =
rt.fn({"path","header_lines"},{rt._missing,rt._missing},function(v_path,v_header_lines)
    local v_f,v_line
    v_f = rt.call(v_open,rt.arguments({v_path,"wt"}),rt.dict({{"encoding","utf-8"}}))
    for temporary_1 in rt.iter(v_header_lines) do
        do
            v_line = temporary_1
            rt.call(rt.attr(v_f,"write"),rt.arguments({rt.add(v_line,"\n")}),rt.dict({}))
        end
        ::temporary_2::
    end
    rt.call(rt.attr(v_f,"write"),rt.arguments({"#pragma once\n\n"}),rt.dict({}))
    do return v_f end
end)
v_generate_gdextension_interface_header =
rt.fn({"target","source","header_lines"},{rt._missing,rt._missing,rt.array({})},function(v_target,v_source,v_header_lines)
    local v_buffer,v_data,v_file,v_function_name,v_handles,v_interface,v_interface_replacements,v_kind,v_replace_with,v_replacement,v_type,v_type_name,v_type_replacements,v_valid_data_types,v_valid_interfaces
    v_buffer = rt.call(v__get_buffer,rt.arguments({v_source}),rt.dict({}))
    v_data = rt.call(rt.attr(v_json,"loads"),rt.arguments({v_buffer}),rt.dict({{"object_pairs_hook",v_OrderedDict}}))
    rt.call(v_check_formatting,rt.arguments({rt.call(rt.attr(v_buffer,"decode"),rt.arguments({"utf-8"}),rt.dict({})),v_data,v_source}),rt.dict({}))
    rt.call(v_check_allowed_keys,rt.arguments({v_data,rt.array({"_copyright","$schema","format_version","types","interface"})}),rt.dict({}))
    v_valid_data_types = rt.dict({})
    for temporary_3 in rt.iter(v_BASE_TYPES) do
        do
            v_type = temporary_3
            rt.put(v_valid_data_types,v_type,true)
        end
        ::temporary_4::
    end
    do
        local v_file = rt.call(v__generated_wrapper,rt.arguments({v_target,v_header_lines}),rt.dict({}))
        rt.call(rt.attr(v_file,"write"),rt.arguments({"#ifndef __cplusplus\n#include <stddef.h>\n#include <stdint.h>\n\ntypedef uint32_t char32_t;\ntypedef uint16_t char16_t;\n#else\n#include <cstddef>\n#include <cstdint>\n\nextern \"C\" {\n#endif\n\n"}),rt.dict({}))
        v_handles = rt.array({})
        v_type_replacements = rt.array({})
        for temporary_5 in rt.iter(rt.get(v_data,"types")) do
            do
                v_type = temporary_5
                v_kind = rt.get(v_type,"kind")
                rt.call(v_check_type,rt.arguments({v_kind,v_type,v_valid_data_types}),rt.dict({}))
                rt.put(v_valid_data_types,rt.get(v_type,"name"),v_type)
                if rt.truth(((rt.contains(v_type,"deprecated")))) then
                    rt.call(v_check_allowed_keys,rt.arguments({rt.get(v_type,"deprecated"),rt.array({"since"}),rt.array({"message","replace_with"})}),rt.dict({}))
                    if rt.truth(((rt.contains(rt.get(v_type,"deprecated"),"replace_with")))) then
                        rt.call(rt.attr(v_type_replacements,"append"),rt.arguments({rt.array({rt.get(v_type,"name"),rt.get(rt.get(v_type,"deprecated"),"replace_with")})}),rt.dict({}))
                    end
                end
                if rt.truth(((rt.contains(v_type,"description")))) then
                    rt.call(v_write_doc,rt.arguments({v_file,rt.get(v_type,"description")}),rt.dict({}))
                end
                if rt.truth((((v_kind=="handle")))) then
                    rt.call(v_check_allowed_keys,rt.arguments({v_type,rt.array({"name","kind"}),rt.array({"is_const","is_uninitialized","parent","description","deprecated"})}),rt.dict({}))
                    if rt.truth(rt.logical(((rt.contains(v_type,"parent"))),function() return ((not rt.contains(v_handles,rt.get(v_type,"parent")))) end,true)) then
                        raise(rt.str(rt.call(v_UnknownTypeError,rt.arguments({rt.get(v_type,"parent"),rt.get(v_type,"name")}),rt.dict({}))))
                    end
                    rt.put(v_type,"type",rt.choose(not rt.truth(rt.call(rt.attr(v_type,"get"),rt.arguments({"is_const",false}),rt.dict({}))),function() return "void*" end,function() return "const void*" end))
                    rt.call(v_write_simple_type,rt.arguments({v_file,v_type}),rt.dict({}))
                    rt.call(rt.attr(v_handles,"append"),rt.arguments({rt.get(v_type,"name")}),rt.dict({}))
                else
                    if rt.truth((((v_kind=="alias")))) then
                        rt.call(v_check_allowed_keys,rt.arguments({v_type,rt.array({"name","kind","type"}),rt.array({"description","deprecated"})}),rt.dict({}))
                        rt.call(v_write_simple_type,rt.arguments({v_file,v_type}),rt.dict({}))
                    else
                        if rt.truth((((v_kind=="enum")))) then
                            rt.call(v_check_allowed_keys,rt.arguments({v_type,rt.array({"name","kind","values"}),rt.array({"is_bitfield","description","deprecated"})}),rt.dict({}))
                            rt.call(v_write_enum_type,rt.arguments({v_file,v_type}),rt.dict({}))
                        else
                            if rt.truth((((v_kind=="function")))) then
                                rt.call(v_check_allowed_keys,rt.arguments({v_type,rt.array({"name","kind","arguments"}),rt.array({"return_value","description","deprecated"})}),rt.dict({}))
                                rt.call(v_write_function_type,rt.arguments({v_file,v_type}),rt.dict({}))
                            else
                                if rt.truth((((v_kind=="struct")))) then
                                    rt.call(v_check_allowed_keys,rt.arguments({v_type,rt.array({"name","kind","members"}),rt.array({"description","deprecated"})}),rt.dict({}))
                                    rt.call(v_write_struct_type,rt.arguments({v_file,v_type}),rt.dict({}))
                                else
                                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Unknown kind of type: " .. rt.str(v_kind))}),rt.dict({}))))
                                end
                            end
                        end
                    end
                end
            end
            ::temporary_6::
        end
        for temporary_7 in rt.iter(v_type_replacements) do
            do
                v_type_name = rt.get(temporary_7,0); v_replace_with = rt.get(temporary_7,1)
                if rt.truth(((not rt.contains(v_valid_data_types,v_replace_with)))) then
                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Unknown type '" .. rt.str(v_replace_with) .. "' used as replacement for '" .. rt.str(v_type_name) .. "'")}),rt.dict({}))))
                end
                v_replacement = rt.get(v_valid_data_types,v_replace_with)
                if rt.truth(rt.logical(rt.call(v_isinstance,rt.arguments({v_replacement,v_dict}),rt.dict({})),function() return ((rt.contains(v_replacement,"deprecated"))) end,true)) then
                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Cannot use '" .. rt.str(v_replace_with) .. "' as replacement for '" .. rt.str(v_type_name) .. "' because it's deprecated too")}),rt.dict({}))))
                end
            end
            ::temporary_8::
        end
        v_interface_replacements = rt.array({})
        v_valid_interfaces = rt.dict({})
        for temporary_9 in rt.iter(rt.get(v_data,"interface")) do
            do
                v_interface = temporary_9
                rt.call(v_check_type,rt.arguments({"function",v_interface,v_valid_data_types}),rt.dict({}))
                rt.call(v_check_allowed_keys,rt.arguments({v_interface,rt.array({"name","arguments","since","description"}),rt.array({"return_value","see","legacy_type_name","deprecated"})}),rt.dict({}))
                rt.put(v_valid_interfaces,rt.get(v_interface,"name"),v_interface)
                if rt.truth(((rt.contains(v_interface,"deprecated")))) then
                    rt.call(v_check_allowed_keys,rt.arguments({rt.get(v_interface,"deprecated"),rt.array({"since"}),rt.array({"message","replace_with"})}),rt.dict({}))
                    if rt.truth(((rt.contains(rt.get(v_interface,"deprecated"),"replace_with")))) then
                        rt.call(rt.attr(v_interface_replacements,"append"),rt.arguments({rt.array({rt.get(v_interface,"name"),rt.get(rt.get(v_interface,"deprecated"),"replace_with")})}),rt.dict({}))
                    end
                end
                rt.call(v_write_interface,rt.arguments({v_file,v_interface}),rt.dict({}))
            end
            ::temporary_10::
        end
        for temporary_11 in rt.iter(v_interface_replacements) do
            do
                v_function_name = rt.get(temporary_11,0); v_replace_with = rt.get(temporary_11,1)
                if rt.truth(((not rt.contains(v_valid_interfaces,v_replace_with)))) then
                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Unknown interface function '" .. rt.str(v_replace_with) .. "' used as replacement for '" .. rt.str(v_function_name) .. "'")}),rt.dict({}))))
                end
                v_replacement = rt.get(v_valid_interfaces,v_replace_with)
                if rt.truth(((rt.contains(v_replacement,"deprecated")))) then
                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Cannot use '" .. rt.str(v_replace_with) .. "' as replacement for '" .. rt.str(v_function_name) .. "' because it's deprecated too")}),rt.dict({}))))
                end
            end
            ::temporary_12::
        end
        rt.call(rt.attr(v_file,"write"),rt.arguments({"#ifdef __cplusplus\n}\n#endif\n"}),rt.dict({}))
        rt.close(v_file)
    end
end)
v_check_formatting =
rt.fn({"buffer","data","filename"},{rt._missing,rt._missing,rt._missing},function(v_buffer,v_data,v_filename)
    local v_buffer2,v_diff,v_lines1,v_lines2
    v_buffer2 = rt.call(rt.attr(v_json,"dumps"),rt.arguments({v_data}),rt.dict({{"indent",4}}))
    v_lines1 = rt.call(rt.attr(v_buffer,"splitlines"),rt.arguments({}),rt.dict({}))
    v_lines2 = rt.call(rt.attr(v_buffer2,"splitlines"),rt.arguments({}),rt.dict({}))
    v_diff = rt.call(rt.attr(v_difflib,"unified_diff"),rt.arguments({v_lines1,v_lines2}),rt.dict({{"fromfile",rt.add("a/",v_filename)},{"tofile",rt.add("b/",v_filename)},{"lineterm",""}}))
    v_diff = rt.call(v_list,rt.arguments({v_diff}),rt.dict({}))
    if rt.truth((((rt.call(v_len,rt.arguments({v_diff}),rt.dict({}))>0)))) then
        rt.call(v_print,rt.arguments({" *** Apply this patch to fix: ***\n"}),rt.dict({}))
        rt.call(v_print,rt.arguments({rt.call(rt.attr("\n","join"),rt.arguments({v_diff}),rt.dict({}))}),rt.dict({}))
        raise(rt.str(rt.call(v_Exception,rt.arguments({("Formatting issues in " .. rt.str(v_filename))}),rt.dict({}))))
    end
end)
v_check_allowed_keys =
rt.fn({"data","required","optional"},{rt._missing,rt._missing,rt.array({})},function(v_data,v_required,v_optional)
    local v_allowed,v_k,v_keys,v_r
    v_keys = rt.call(rt.attr(v_data,"keys"),rt.arguments({}),rt.dict({}))
    v_allowed = rt.add(v_required,v_optional)
    for temporary_13 in rt.iter(v_keys) do
        do
            v_k = temporary_13
            if rt.truth(((not rt.contains(v_allowed,v_k)))) then
                raise(rt.str(rt.call(v_Exception,rt.arguments({("Found unknown key '" .. rt.str(v_k) .. "'")}),rt.dict({}))))
            end
        end
        ::temporary_14::
    end
    for temporary_15 in rt.iter(v_required) do
        do
            v_r = temporary_15
            if rt.truth(((not rt.contains(v_keys,v_r)))) then
                raise(rt.str(rt.call(v_Exception,rt.arguments({("Missing required key '" .. rt.str(v_r) .. "'")}),rt.dict({}))))
            end
        end
        ::temporary_16::
    end
end)
v_base_type_name =
rt.fn({"type_name"},{rt._missing},function(v_type_name)
    if rt.truth(rt.call(rt.attr(v_type_name,"startswith"),rt.arguments({"const "}),rt.dict({}))) then
        v_type_name = rt.slice(v_type_name,6,rt._none,rt._none)
    end
    if rt.truth(rt.call(rt.attr(v_type_name,"endswith"),rt.arguments({"*"}),rt.dict({}))) then
        v_type_name = rt.slice(v_type_name,rt._none,(-1),rt._none)
    end
    do return v_type_name end
end)
v_format_type_and_name =
rt.fn({"type","name"},{rt._missing,rt._none},function(v_type,v_name)
    local v_ret
    v_ret = v_type
    if rt.truth((((rt.get(v_ret,(-1))=="*")))) then
        v_ret = rt.add(rt.slice(v_ret,rt._none,(-1),rt._none)," *")
    end
    if rt.truth(v_name) then
        if rt.truth((((rt.get(v_ret,(-1))=="*")))) then
            v_ret = rt.add(v_ret,v_name)
        else
            v_ret = rt.add(rt.add(v_ret," "),v_name)
        end
    end
    do return v_ret end
end)
v_is_valid_type =
rt.fn({"type","valid_data_types"},{rt._missing,rt._missing},function(v_type,v_valid_data_types)
    if rt.truth(((rt.contains(rt.array({"void","const void"}),v_type)))) then
        do return false end
    end
    do return ((rt.contains(v_valid_data_types,rt.call(v_base_type_name,rt.arguments({v_type}),rt.dict({}))))) end
end)
v_check_type =
rt.fn({"kind","type","valid_data_types"},{rt._missing,rt._missing,rt._missing},function(v_kind,v_type,v_valid_data_types)
    local v_arg,v_member
    if rt.truth((((v_kind=="alias")))) then
        if rt.truth(not rt.truth(rt.call(v_is_valid_type,rt.arguments({rt.get(v_type,"type"),v_valid_data_types}),rt.dict({})))) then
            raise(rt.str(rt.call(v_UnknownTypeError,rt.arguments({rt.get(v_type,"type"),rt.get(v_type,"name")}),rt.dict({}))))
        end
    else
        if rt.truth((((v_kind=="struct")))) then
            for temporary_17 in rt.iter(rt.get(v_type,"members")) do
                do
                    v_member = temporary_17
                    if rt.truth(not rt.truth(rt.call(v_is_valid_type,rt.arguments({rt.get(v_member,"type"),v_valid_data_types}),rt.dict({})))) then
                        raise(rt.str(rt.call(v_UnknownTypeError,rt.arguments({rt.get(v_member,"type"),rt.get(v_type,"name"),rt.get(v_member,"name")}),rt.dict({}))))
                    end
                end
                ::temporary_18::
            end
        else
            if rt.truth((((v_kind=="function")))) then
                for temporary_19 in rt.iter(rt.get(v_type,"arguments")) do
                    do
                        v_arg = temporary_19
                        if rt.truth(not rt.truth(rt.call(v_is_valid_type,rt.arguments({rt.get(v_arg,"type"),v_valid_data_types}),rt.dict({})))) then
                            raise(rt.str(rt.call(v_UnknownTypeError,rt.arguments({rt.get(v_arg,"type"),rt.get(v_type,"name"),rt.call(rt.attr(v_arg,"get"),rt.arguments({"name"}),rt.dict({}))}),rt.dict({}))))
                        end
                    end
                    ::temporary_20::
                end
                if rt.truth(((rt.contains(v_type,"return_value")))) then
                    if rt.truth(not rt.truth(rt.call(v_is_valid_type,rt.arguments({rt.get(rt.get(v_type,"return_value"),"type"),v_valid_data_types}),rt.dict({})))) then
                        raise(rt.str(rt.call(v_UnknownTypeError,rt.arguments({rt.get(rt.get(v_type,"return_value"),"type"),rt.get(v_type,"name")}),rt.dict({}))))
                    end
                end
            end
        end
    end
end)
v_write_doc =
rt.fn({"file","doc","indent"},{rt._missing,rt._missing,""},function(v_file,v_doc,v_indent)
    local v_first,v_line
    if rt.truth((((rt.call(v_len,rt.arguments({v_doc}),rt.dict({}))==1)))) then
        rt.call(rt.attr(v_file,"write"),rt.arguments({(rt.str(v_indent) .. "/* " .. rt.str(rt.get(v_doc,0)) .. " */\n")}),rt.dict({}))
        do return rt._none end
    end
    v_first = true
    for temporary_21 in rt.iter(v_doc) do
        do
            v_line = temporary_21
            if rt.truth(v_first) then
                rt.call(rt.attr(v_file,"write"),rt.arguments({rt.add(v_indent,"/*")}),rt.dict({}))
                v_first = false
            else
                rt.call(rt.attr(v_file,"write"),rt.arguments({rt.add(v_indent," *")}),rt.dict({}))
            end
            if rt.truth((((v_line~="")))) then
                rt.call(rt.attr(v_file,"write"),rt.arguments({rt.add(" ",v_line)}),rt.dict({}))
            end
            rt.call(rt.attr(v_file,"write"),rt.arguments({"\n"}),rt.dict({}))
        end
        ::temporary_22::
    end
    rt.call(rt.attr(v_file,"write"),rt.arguments({rt.add(v_indent," */\n")}),rt.dict({}))
end)
v_make_deprecated_message =
rt.fn({"data"},{rt._missing},function(v_data)
    local v_parts,v_x
    v_parts = rt.array({("Deprecated in Godot " .. rt.str(rt.get(v_data,"since")) .. "."),rt.choose(((rt.contains(v_data,"message"))),function() return rt.get(v_data,"message") end,function() return "" end),rt.choose(((rt.contains(v_data,"replace_with"))),function() return ("Use `" .. rt.str(rt.get(v_data,"replace_with")) .. "` instead.") end,function() return "" end)})
    do return rt.call(rt.attr(" ","join"),rt.arguments({(function() local temporary_23=rt.array({}); for temporary_24 in rt.iter(v_parts) do v_x = temporary_24; if rt.truth((((rt.call(rt.attr(v_x,"strip"),rt.arguments({}),rt.dict({}))~="")))) then rt.append(temporary_23,v_x) end end; return temporary_23 end)()}),rt.dict({})) end
end)
v_make_deprecated_comment_for_type =
rt.fn({"type"},{rt._missing},function(v_type)
    local v_message
    if rt.truth(((not rt.contains(v_type,"deprecated")))) then
        do return "" end
    end
    v_message = rt.call(v_make_deprecated_message,rt.arguments({rt.get(v_type,"deprecated")}),rt.dict({}))
    do return (" /* " .. rt.str(v_message) .. " */") end
end)
v_write_simple_type =
rt.fn({"file","type"},{rt._missing,rt._missing},function(v_file,v_type)
    rt.call(rt.attr(v_file,"write"),rt.arguments({("typedef " .. rt.str(rt.call(v_format_type_and_name,rt.arguments({rt.get(v_type,"type"),rt.get(v_type,"name")}),rt.dict({}))) .. ";" .. rt.str(rt.call(v_make_deprecated_comment_for_type,rt.arguments({v_type}),rt.dict({}))) .. "\n")}),rt.dict({}))
end)
v_write_enum_type =
rt.fn({"file","enum"},{rt._missing,rt._missing},function(v_file,v_enum)
    local v_value
    rt.call(rt.attr(v_file,"write"),rt.arguments({"typedef enum {\n"}),rt.dict({}))
    for temporary_25 in rt.iter(rt.get(v_enum,"values")) do
        do
            v_value = temporary_25
            rt.call(v_check_allowed_keys,rt.arguments({v_value,rt.array({"name","value"}),rt.array({"description","deprecated"})}),rt.dict({}))
            if rt.truth(((rt.contains(v_value,"description")))) then
                rt.call(v_write_doc,rt.arguments({v_file,rt.get(v_value,"description"),"\t"}),rt.dict({}))
            end
            rt.call(rt.attr(v_file,"write"),rt.arguments({("\t" .. rt.str(rt.get(v_value,"name")) .. " = " .. rt.str(rt.get(v_value,"value")) .. ",\n")}),rt.dict({}))
        end
        ::temporary_26::
    end
    rt.call(rt.attr(v_file,"write"),rt.arguments({("} " .. rt.str(rt.get(v_enum,"name")) .. ";" .. rt.str(rt.call(v_make_deprecated_comment_for_type,rt.arguments({v_enum}),rt.dict({}))) .. "\n\n")}),rt.dict({}))
end)
v_make_args_text =
rt.fn({"args"},{rt._missing},function(v_args)
    local v_arg,v_combined
    v_combined = rt.array({})
    for temporary_27 in rt.iter(v_args) do
        do
            v_arg = temporary_27
            rt.call(v_check_allowed_keys,rt.arguments({v_arg,rt.array({"type"}),rt.array({"name","description"})}),rt.dict({}))
            rt.call(rt.attr(v_combined,"append"),rt.arguments({rt.call(v_format_type_and_name,rt.arguments({rt.get(v_arg,"type"),rt.call(rt.attr(v_arg,"get"),rt.arguments({"name"}),rt.dict({}))}),rt.dict({}))}),rt.dict({}))
        end
        ::temporary_28::
    end
    do return rt.call(rt.attr(", ","join"),rt.arguments({v_combined}),rt.dict({})) end
end)
v_write_function_type =
rt.fn({"file","fn"},{rt._missing,rt._missing},function(v_file,v_fn)
    local v_args_text,v_name_and_args,v_return_type
    v_args_text = rt.choose(((rt.contains(v_fn,"arguments"))),function() return rt.call(v_make_args_text,rt.arguments({rt.get(v_fn,"arguments")}),rt.dict({})) end,function() return "" end)
    v_name_and_args = ("(*" .. rt.str(rt.get(v_fn,"name")) .. ")(" .. rt.str(v_args_text) .. ")")
    v_return_type = rt.choose(((rt.contains(v_fn,"return_value"))),function() return rt.get(rt.get(v_fn,"return_value"),"type") end,function() return "void" end)
    rt.call(rt.attr(v_file,"write"),rt.arguments({("typedef " .. rt.str(rt.call(v_format_type_and_name,rt.arguments({v_return_type,v_name_and_args}),rt.dict({}))) .. ";" .. rt.str(rt.call(v_make_deprecated_comment_for_type,rt.arguments({v_fn}),rt.dict({}))) .. "\n")}),rt.dict({}))
end)
v_write_struct_type =
rt.fn({"file","struct"},{rt._missing,rt._missing},function(v_file,v_struct)
    local v_member
    rt.call(rt.attr(v_file,"write"),rt.arguments({"typedef struct {\n"}),rt.dict({}))
    for temporary_29 in rt.iter(rt.get(v_struct,"members")) do
        do
            v_member = temporary_29
            rt.call(v_check_allowed_keys,rt.arguments({v_member,rt.array({"name","type"}),rt.array({"description"})}),rt.dict({}))
            if rt.truth(((rt.contains(v_member,"description")))) then
                rt.call(v_write_doc,rt.arguments({v_file,rt.get(v_member,"description"),"\t"}),rt.dict({}))
            end
            rt.call(rt.attr(v_file,"write"),rt.arguments({("\t" .. rt.str(rt.call(v_format_type_and_name,rt.arguments({rt.get(v_member,"type"),rt.get(v_member,"name")}),rt.dict({}))) .. ";\n")}),rt.dict({}))
        end
        ::temporary_30::
    end
    rt.call(rt.attr(v_file,"write"),rt.arguments({("} " .. rt.str(rt.get(v_struct,"name")) .. ";" .. rt.str(rt.call(v_make_deprecated_comment_for_type,rt.arguments({v_struct}),rt.dict({}))) .. "\n\n")}),rt.dict({}))
end)
v_write_interface =
rt.fn({"file","interface"},{rt._missing,rt._missing},function(v_file,v_interface)
    local v_arg,v_arg_doc,v_d,v_doc,v_fn,v_ret_doc,v_see,v_word
    v_doc = rt.array({("@name " .. rt.str(rt.get(v_interface,"name"))),("@since " .. rt.str(rt.get(v_interface,"since")))})
    if rt.truth(((rt.contains(v_interface,"deprecated")))) then
        rt.call(rt.attr(v_doc,"append"),rt.arguments({("@deprecated " .. rt.str(rt.call(v_make_deprecated_message,rt.arguments({rt.get(v_interface,"deprecated")}),rt.dict({}))))}),rt.dict({}))
    end
    v_doc = rt.add(v_doc,rt.array({"",rt.get(rt.get(v_interface,"description"),0)}))
    if rt.truth((((rt.call(v_len,rt.arguments({rt.get(v_interface,"description")}),rt.dict({}))>1)))) then
        rt.call(rt.attr(v_doc,"append"),rt.arguments({""}),rt.dict({}))
        v_doc = rt.add(v_doc,rt.slice(rt.get(v_interface,"description"),1,rt._none,rt._none))
    end
    if rt.truth(((rt.contains(v_interface,"arguments")))) then
        rt.call(rt.attr(v_doc,"append"),rt.arguments({""}),rt.dict({}))
        for temporary_31 in rt.iter(rt.get(v_interface,"arguments")) do
            do
                v_arg = temporary_31
                if rt.truth(((not rt.contains(v_arg,"description")))) then
                    raise(rt.str(rt.call(v_Exception,rt.arguments({("Interface function " .. rt.str(rt.get(v_interface,"name")) .. " is missing docs for " .. rt.str(rt.get(v_arg,"name")) .. " argument")}),rt.dict({}))))
                end
                v_arg_doc = rt.call(rt.attr(" ","join"),rt.arguments({rt.get(v_arg,"description")}),rt.dict({}))
                rt.call(rt.attr(v_doc,"append"),rt.arguments({("@param " .. rt.str(rt.get(v_arg,"name")) .. " " .. rt.str(v_arg_doc))}),rt.dict({}))
            end
            ::temporary_32::
        end
    end
    if rt.truth(((rt.contains(v_interface,"return_value")))) then
        if rt.truth(((not rt.contains(rt.get(v_interface,"return_value"),"description")))) then
            raise(rt.str(rt.call(v_Exception,rt.arguments({("Interface function " .. rt.str(rt.get(v_interface,"name")) .. " is missing docs for return value")}),rt.dict({}))))
        end
        v_ret_doc = rt.call(rt.attr(" ","join"),rt.arguments({rt.get(rt.get(v_interface,"return_value"),"description")}),rt.dict({}))
        rt.call(rt.attr(v_doc,"append"),rt.arguments({""}),rt.dict({}))
        rt.call(rt.attr(v_doc,"append"),rt.arguments({("@return " .. rt.str(v_ret_doc))}),rt.dict({}))
    end
    if rt.truth(((rt.contains(v_interface,"see")))) then
        rt.call(rt.attr(v_doc,"append"),rt.arguments({""}),rt.dict({}))
        for temporary_33 in rt.iter(rt.get(v_interface,"see")) do
            do
                v_see = temporary_33
                rt.call(rt.attr(v_doc,"append"),rt.arguments({("@see " .. rt.str(v_see))}),rt.dict({}))
            end
            ::temporary_34::
        end
    end
    rt.call(rt.attr(v_file,"write"),rt.arguments({"/**\n"}),rt.dict({}))
    for temporary_35 in rt.iter(v_doc) do
        do
            v_d = temporary_35
            if rt.truth((((v_d~="")))) then
                rt.call(rt.attr(v_file,"write"),rt.arguments({(" * " .. rt.str(v_d) .. "\n")}),rt.dict({}))
            else
                rt.call(rt.attr(v_file,"write"),rt.arguments({" *\n"}),rt.dict({}))
            end
        end
        ::temporary_36::
    end
    rt.call(rt.attr(v_file,"write"),rt.arguments({" */\n"}),rt.dict({}))
    v_fn = rt.call(rt.attr(v_interface,"copy"),rt.arguments({}),rt.dict({}))
    if rt.truth(((rt.contains(v_fn,"deprecated")))) then
        rt.put(v_fn,"deprecated",nil)
    end
    rt.put(v_fn,"name",rt.add("GDExtensionInterface",rt.call(rt.attr("","join"),rt.arguments({(function() local temporary_37=rt.array({}); for temporary_38 in rt.iter(rt.call(rt.attr(rt.get(v_interface,"name"),"split"),rt.arguments({"_"}),rt.dict({}))) do v_word = temporary_38; if true then rt.append(temporary_37,rt.call(rt.attr(v_word,"capitalize"),rt.arguments({}),rt.dict({}))) end end; return temporary_37 end)()}),rt.dict({}))))
    rt.call(v_write_function_type,rt.arguments({v_file,v_fn}),rt.dict({}))
    rt.call(rt.attr(v_file,"write"),rt.arguments({"\n"}),rt.dict({}))
end)
function main(target,source,header_lines)
    return rt.call(v_generate_gdextension_interface_header,rt.array({target,source,header_lines or rt.array({})}),rt.dict({}))
end
