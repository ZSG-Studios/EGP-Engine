set_xmakever("3.0.0")
add_rules("mode.debug", "mode.release")
for _, name in ipairs({"egp_cpp_sdk", "egp_cpp_library", "egp_fixture_bin"}) do
    option(name)
        set_showmenu(true)
    option_end()
end
for version = 0, 5 do
    target(version == 0 and "observer" or "victim" .. version)
        set_kind("shared")
        set_languages("cxx17")
        set_prefixname("")
        set_targetdir(get_config("egp_fixture_bin"))
        add_files(version == 0 and "observer.cpp" or "victim.cpp")
        add_defines("BINDING_VERSION=" .. version, "GDEXTENSION", "THREADS_ENABLED", "TYPED_METHOD_BIND", "NOMINMAX")
        if is_mode("debug") then add_defines("DEBUG_ENABLED", "HOT_RELOAD_ENABLED") end
        add_includedirs(path.join(get_config("egp_cpp_sdk"), "include"), path.join(get_config("egp_cpp_sdk"), "gen/include"))
        add_ldflags(get_config("egp_cpp_library"), {force = true})
        if is_plat("windows") then set_runtimes(is_mode("debug") and "MDd" or "MD") end
        add_cxxflags("/utf-8", "/Zc:__cplusplus", {tools = {"cl", "clang_cl"}})
    target_end()
end
