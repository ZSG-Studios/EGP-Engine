-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local common_linuxbsd, env, platform_linuxbsd_builders, prog
    env = graph:use("env")
    platform_linuxbsd_builders = graph:builders("platform_linuxbsd_builders")
    common_linuxbsd = {"crash_handler_linuxbsd.cpp", "os_linuxbsd.cpp", "freedesktop_portal_desktop.cpp", "freedesktop_screensaver.cpp", "freedesktop_at_spi_monitor.cpp"}
    if R.truthy(((R.index(env, "library_type") == "executable"))) then
        common_linuxbsd = R.iadd(common_linuxbsd, {"godot_linuxbsd.cpp"})
    else
        common_linuxbsd = R.iadd(common_linuxbsd, {"libgodot_linuxbsd.cpp"})
    end
    if R.truthy(R.index(env, "use_sowrap")) then
        R.append(common_linuxbsd, "xkbcommon-so_wrap.c")
    end
    if R.truthy(R.index(env, "x11")) then
        common_linuxbsd = R.iadd(common_linuxbsd, graph:include("x11/recipe.lua"))
    end
    if R.truthy(R.index(env, "wayland")) then
        common_linuxbsd = R.iadd(common_linuxbsd, graph:include("wayland/recipe.lua"))
    end
    if R.truthy(R.index(env, "speechd")) then
        R.append(common_linuxbsd, "tts_linux.cpp")
        if R.truthy(R.index(env, "use_sowrap")) then
            R.append(common_linuxbsd, "speechd-so_wrap.c")
        end
    end
    if R.truthy(R.index(env, "fontconfig")) then
        if R.truthy(R.index(env, "use_sowrap")) then
            R.append(common_linuxbsd, "fontconfig-so_wrap.c")
        end
    end
    if R.truthy(R.index(env, "dbus")) then
        if R.truthy(R.index(env, "use_sowrap")) then
            R.append(common_linuxbsd, "dbus-so_wrap.c")
        end
    end
    if R.truthy(((R.index(env, "library_type") == "static_library"))) then
        prog = env:library("#bin/godot", common_linuxbsd)
    else
        if R.truthy(((R.index(env, "library_type") == "shared_library"))) then
            prog = env:shared_library("#bin/godot", common_linuxbsd)
        else
            prog = env:program("#bin/godot", common_linuxbsd)
        end
    end
    if R.truthy((function() local v = R.index(env, "debug_symbols"); if not R.truthy(v) then return v end; return R.index(env, "separate_debug_symbols") end)()) then
        env:after_build(prog, env:generator(platform_linuxbsd_builders.make_debug_linuxbsd))
    end
end
