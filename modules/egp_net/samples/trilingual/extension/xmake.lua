set_xmakever("3.0.0")
add_rules("mode.debug", "mode.release")
option("egp_cpp_sdk")
    set_showmenu(true)
option_end()
for _, name in ipairs({"egp_cpp_cache", "egp_cpp_library"}) do
    option(name)
        set_default("")
        set_showmenu(true)
    option_end()
end
local sdk = get_config("egp_cpp_sdk")
if sdk then includes(path.join(sdk, "xmake.lua")) end
target("extension")
    set_kind("shared")
    set_languages("cxx17")
    set_prefixname("")
    set_basename(is_mode("release") and "netinterop.release" or "netinterop")
    set_targetdir("bin")
    if is_plat("windows") then set_runtimes(is_mode("debug") and "MDd" or "MD") end
    add_files("probe.cpp")
    add_includedirs("../addons/egp_net/cpp")
    add_deps("godot-cpp")
target_end()
