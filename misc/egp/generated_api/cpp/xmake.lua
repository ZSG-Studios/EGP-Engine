set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags", false)
add_rules("mode.debug", "mode.release")
option("egp_cpp_sdk")
    set_showmenu(true)
option_end()
target("api-probe")
    set_kind("object")
    set_languages("cxx17")
    add_files("probe.cpp")
    add_includedirs("../../../..")
    on_load(function (target)
        import("core.base.json")
        local sdk = get_config("egp_cpp_sdk")
        assert(sdk and os.isfile(path.join(sdk, "sdk.json")), "Choose the matching extracted editor SDK")
        local metadata = json.loadfile(path.join(sdk, "sdk.json"))
        target:add("sysincludedirs", path.join(sdk, "include"), path.join(sdk, "gen/include"))
        target:add("defines", "GDEXTENSION", "THREADS_ENABLED")
        if metadata.precision == "double" then target:add("defines", "REAL_T_IS_DOUBLE") end
        if is_mode("debug") then target:add("defines", "DEBUG_ENABLED", "HOT_RELOAD_ENABLED") end
        if is_plat("windows") then
            target:add("defines", "TYPED_METHOD_BIND", "NOMINMAX")
            target:add("cxxflags", "/utf-8", "/Zc:__cplusplus", "/W4", "/WX", {tools = {"cl", "clang_cl"}})
        else
            target:add("cxxflags", "-Wall", "-Wextra", "-Werror")
        end
    end)
target_end()
