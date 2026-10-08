-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_openxr, env_thirdparty, khrloader_obj, module_obj, sys, thirdparty_dir, thirdparty_jsoncpp_dir, thirdparty_obj
    sys = R.sys
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_openxr = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(((R.index(env, "platform") == "android"))) then
        env_openxr:add_unique({["CPPDEFINES"] = {"XR_OS_ANDROID", "XR_USE_PLATFORM_ANDROID"}})
        env_openxr:add_unique({["CPPDEFINES"] = {{"JSON_USE_EXCEPTION", 0}}})
    else
        if R.truthy(((R.index(env, "platform") == "linuxbsd"))) then
            env_openxr:add_unique({["CPPDEFINES"] = {"XR_OS_LINUX"}})
            if R.truthy(R.index(env, "x11")) then
                env_openxr:add_unique({["CPPDEFINES"] = {"XR_USE_PLATFORM_XLIB"}})
            end
            if R.truthy(not R.truthy(R.startswith(sys.platform, "freebsd"))) then
                env_openxr:add_unique({["CPPDEFINES"] = {"HAVE_SECURE_GETENV"}})
            end
        else
            if R.truthy(((R.index(env, "platform") == "windows"))) then
                env_openxr:add_unique({["CPPDEFINES"] = {"XR_OS_WINDOWS", "NOMINMAX", "XR_USE_PLATFORM_WIN32"}})
            else
                if R.truthy(((R.index(env, "platform") == "macos"))) then
                    env_openxr:add_unique({["CPPDEFINES"] = {"XR_OS_APPLE"}})
                end
            end
        end
    end
    if R.truthy(R.index(env, "builtin_openxr")) then
        thirdparty_dir = "#thirdparty/openxr"
        env_openxr:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "/include"), R.add(thirdparty_dir, "/src"), R.add(thirdparty_dir, "/src/common"), R.add(thirdparty_dir, "/src/external/jsoncpp/include")}})
        env_thirdparty = env_openxr:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:add_unique({["CPPDEFINES"] = {"DISABLE_STD_FILESYSTEM"}})
        if R.truthy(R.index(env, "disable_exceptions")) then
            env_thirdparty:add_unique({["CPPDEFINES"] = {"XRLOADER_DISABLE_EXCEPTION_HANDLING", {"JSON_USE_EXCEPTION", 0}}})
        end
        env_thirdparty:add({["CPPPATH"] = {R.add(thirdparty_dir, "/src/loader")}})
        thirdparty_jsoncpp_dir = R.add(thirdparty_dir, "/src/external/jsoncpp/src/lib_json/")
        env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_jsoncpp_dir, "json_reader.cpp"))
        env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_jsoncpp_dir, "json_value.cpp"))
        env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_jsoncpp_dir, "json_writer.cpp"))
        if R.truthy(((R.index(env, "platform") ~= "android"))) then
            khrloader_obj = {}
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/xr_generated_dispatch_table_core.c"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/common/filesystem_utils.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/common/object_info.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/api_layer_interface.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_core.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_init_data.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_instance.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_logger_recorders.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_logger.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/loader_properties.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/manifest_file.cpp"))
            env_thirdparty:sources(khrloader_obj, R.add(thirdparty_dir, "/src/loader/runtime_interface.cpp"))
            env.modules_sources = R.iadd(env.modules_sources, khrloader_obj)
        end
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_openxr:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    graph:publish("env_openxr", env_openxr)
    graph:include("action_map/recipe.lua")
    graph:include("extensions/recipe.lua")
    graph:include("scene/recipe.lua")
    if R.truthy(env.editor_build) then
        graph:include("editor/recipe.lua")
    end
    env:depends(module_obj, thirdparty_obj)
end
