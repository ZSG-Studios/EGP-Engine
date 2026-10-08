-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local Path, agility_arch_subdir, agility_dlls, agility_target_aliases, arch_bin_dir, arrange_program_clean, common_win, common_win_wrap, dll, dxc_arch_subdir, dxc_target_aliases, env, env_cpp_check, env_wrap, os, pix_dll, platform_windows_builders, prog, prog_wrap, redirect_emitter, res_file, res_obj, res_target, res_wrap_file, res_wrap_obj, res_wrap_target, sources, target_dir
    env = graph:use("env")
    os = R.os
    Path = R.Path
    platform_windows_builders = graph:builders("platform_windows_builders")
    redirect_emitter = graph:methods().redirect_emitter
    sources = {}
    common_win = {"os_windows.cpp", "display_server_windows.cpp", "key_mapping_windows.cpp", "windows_terminal_logger.cpp", "windows_utils.cpp", "native_menu_windows.cpp", "gl_manager_windows_native.cpp", "wgl_detect_version.cpp", "rendering_context_driver_vulkan_windows.cpp", "drop_target_windows.cpp", "winrt_utils.cpp", "tts_windows.cpp", "tts_driver_sapi.cpp", "tts_driver_onecore.cpp"}
    if R.truthy(R.index(env, "angle")) then
        common_win = R.iadd(common_win, {"gl_manager_windows_angle.cpp"})
    end
    if R.truthy(((R.index(env, "library_type") == "executable"))) then
        common_win = R.iadd(common_win, {"godot_windows.cpp"})
    else
        common_win = R.iadd(common_win, {"libgodot_windows.cpp"})
    end
    if R.truthy(env.msvc) then
        common_win = R.iadd(common_win, {"crash_handler_windows_seh.cpp"})
    else
        common_win = R.iadd(common_win, {"crash_handler_windows_signal.cpp"})
    end
    common_win_wrap = {"console_wrapper_windows.cpp"}
    env_wrap = env:clone()
    if R.truthy((function() local v = ((R.index(env, "arch") == "x86_64")); if not R.truthy(v) then return v end; return ((R.index(env, "library_type") == "executable")) end)()) then
        env_cpp_check = env:clone()
        env_cpp_check:sources(sources, {"cpu_feature_validation.c"})
        if R.truthy(env.msvc) then
            if R.truthy((R.contains(R.index(env_cpp_check, "CCFLAGS"), "/d2archSSE42"))) then
                R.remove(R.index(env_cpp_check, "CCFLAGS"), "/d2archSSE42")
            end
            env:add({["LINKFLAGS"] = {"/ENTRY:ShimMainCRTStartup"}})
        else
            if R.truthy((R.contains(R.index(env_cpp_check, "CCFLAGS"), "-msse4.2"))) then
                R.remove(R.index(env_cpp_check, "CCFLAGS"), "-msse4.2")
            end
            env:add({["LINKFLAGS"] = {"-Wl,--entry=ShimMainCRTStartup"}})
        end
    end
    arrange_program_clean = function(prog)
        local executable_stem, extensions_to_clean, extra_files_to_clean, program
        extensions_to_clean = {".ilk", ".exp", ".pdb", ".lib"}
        for _, __item1 in ipairs(R.iter(prog)) do
            program = __item1
            executable_stem = R.Path(program.name).stem
            extra_files_to_clean = (function() local __item2 = {}; for _, __item3 in ipairs(R.iter(extensions_to_clean)) do; local extension = __item3; table.insert(__item2, R.join({"#bin/", R.str(executable_stem), R.str(extension)}, "")); end; return __item2 end)()
            graph:clean(prog, extra_files_to_clean)
        end
    end
    R.index(R.index(env, "BUILDERS"), "RES").emitter = redirect_emitter
    if R.truthy(env.editor_build) then
        res_file = "godot_res.rc"
        res_target = R.add("godot_res", R.index(env, "OBJSUFFIX"))
    else
        res_file = "godot_res_template.rc"
        res_target = R.add("godot_res_template", R.index(env, "OBJSUFFIX"))
    end
    res_obj = env:resource(res_target, res_file)
    env:depends(res_obj, "#core/version_generated.gen.h")
    env:sources(sources, common_win)
    sources = R.iadd(sources, res_obj)
    if R.truthy(((R.index(env, "library_type") == "static_library"))) then
        prog = env:library("#bin/godot", sources, {["PROGSUFFIX"] = R.index(env, "PROGSUFFIX")})
    else
        if R.truthy(((R.index(env, "library_type") == "shared_library"))) then
            prog = env:shared_library("#bin/godot", sources, {["PROGSUFFIX"] = R.index(env, "PROGSUFFIX")})
        else
            prog = env:program("#bin/godot", sources, {["PROGSUFFIX"] = R.index(env, "PROGSUFFIX")})
        end
    end
    arrange_program_clean(prog)
    env:depends(prog, "godot.manifest")
    if R.truthy(env.msvc) then
        env:depends(prog, "godot.natvis")
    end
    if R.truthy(((R.index(env, "windows_subsystem") == "gui"))) and env.options.library_type == "executable" then
        if R.truthy(env.editor_build) then
            res_wrap_file = "godot_res_wrap.rc"
            res_wrap_target = R.add("godot_res_wrap", R.index(env, "OBJSUFFIX"))
        else
            res_wrap_file = "godot_res_wrap_template.rc"
            res_wrap_target = R.add("godot_res_wrap_template", R.index(env, "OBJSUFFIX"))
        end
        res_wrap_obj = env_wrap:resource(res_wrap_target, res_wrap_file)
        env_wrap:depends(res_wrap_obj, "#core/version_generated.gen.h")
        if R.truthy(env.msvc) then
            env_wrap:add({["LINKFLAGS"] = {"/SUBSYSTEM:CONSOLE"}})
            env_wrap:add({["LINKFLAGS"] = {"version.lib"}})
        else
            env_wrap:add({["LINKFLAGS"] = {"-Wl,--subsystem,console"}})
            env_wrap:add({["LIBS"] = {"version"}})
        end
        prog_wrap = env_wrap:program("#bin/godot", R.add(common_win_wrap, res_wrap_obj), {["PROGSUFFIX"] = R.index(env, "PROGSUFFIX_WRAP")})
        arrange_program_clean(prog_wrap)
        env_wrap:depends(prog_wrap, prog)
        sources = R.iadd(sources, R.add(common_win_wrap, res_wrap_obj))
    end
    if R.truthy(R.index(env, "d3d12")) then
        dxc_target_aliases = R.dict({["x86_32"] = "x86", ["x86_64"] = "x64", ["arm32"] = "arm", ["arm64"] = "arm64"})
        dxc_arch_subdir = R.index(dxc_target_aliases, R.index(env, "arch"))
        agility_target_aliases = R.dict({["x86_32"] = "win32", ["x86_64"] = "x64", ["arm32"] = "arm", ["arm64"] = "arm64"})
        agility_arch_subdir = R.index(agility_target_aliases, R.index(env, "arch"))
        arch_bin_dir = R.add("#bin/", R.index(env, "arch"))
        if R.truthy((function() local v = ((R.index(env, "agility_sdk_path") ~= "")); if not R.truthy(v) then return v end; return os.path.exists(R.index(env, "agility_sdk_path")) end)()) then
            agility_dlls = {"D3D12Core.dll", "d3d12SDKLayers.dll"}
            target_dir = (function() if R.truthy(R.index(env, "agility_sdk_multiarch")) then return arch_bin_dir else return "#bin" end end)()
            for _, __item4 in ipairs(R.iter(agility_dlls)) do
                dll = __item4
                env:generate(R.add(R.add(target_dir, "/"), dll), R.add(R.add(R.add(R.add(R.index(env, "agility_sdk_path"), "/build/native/bin/"), agility_arch_subdir), "/"), dll), graph:copy("$TARGET", "$SOURCE"))
            end
        end
        if R.truthy(R.index(env, "use_pix")) then
            pix_dll = "WinPixEventRuntime.dll"
            env:generate(R.add("#bin/", pix_dll), R.add(R.add(R.add(R.add(R.index(env, "pix_path"), "/bin/"), dxc_arch_subdir), "/"), pix_dll), graph:copy("$TARGET", "$SOURCE"))
        end
    end
    if R.truthy(not R.truthy(env.msvc)) then
        if R.truthy(R.index(env, "debug_symbols")) then
            env:after_build(prog, env:generator(platform_windows_builders.make_debug_mingw))
            if R.truthy(((R.index(env, "windows_subsystem") == "gui"))) then
                env:after_build(prog_wrap, env:generator(platform_windows_builders.make_debug_mingw))
            end
        end
    end
    env.platform_sources = R.iadd(env.platform_sources, sources)
end
