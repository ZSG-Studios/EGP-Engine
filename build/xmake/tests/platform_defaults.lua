-- Default source-selection policy must follow the selected platform rather than the host capture.
function main()
    local root = os.projectdir()
    local model = import("build.xmake.graph", {rootdir = root})
    local common = import("build.xmake.common", {rootdir = root})
    local checks = 0
    local function check(value) assert(value); checks = checks + 1 end
    local function resolve(options)
        local graph = model.new(root, options)
        common.configure(graph.environment, graph.options, graph.explicit)
        return graph.options, graph.environment
    end
    local linux = resolve({platform = "linuxbsd", arch = "x86_64"})
    for _, name in ipairs({"alsa", "pulseaudio", "dbus", "speechd", "fontconfig", "udev", "x11", "wayland", "use_sowrap", "libdecor", "touch"}) do check(linux[name] == true) end
    check(linux.execinfo == false)
    local disabled = resolve({platform = "linuxbsd", arch = "x86_64", x11 = "n", wayland = "false", use_sowrap = "0", alsa = "no"})
    check(disabled.x11 == false and disabled.wayland == false and disabled.use_sowrap == false and disabled.alsa == false)
    local mac, macenv = resolve({platform = "macos", arch = "arm64"})
    check(mac.metal == true and mac.use_volk == false and table.contains(macenv.supported, "metal"))
    local ios = resolve({platform = "ios"})
    check(ios.arch == "arm64" and ios.target == "template_debug" and ios.metal == true and ios.builtin_pcre2_with_jit == false)
    local vision = resolve({platform = "visionos"})
    check(vision.arch == "arm64" and vision.metal == true and vision.vulkan == false and vision.opengl3 == false)
    local web = resolve({platform = "web"})
    check(web.arch == "wasm32" and web.target == "template_debug" and web.optimize == "size")
    check(web.javascript_eval == true and web.wasm_simd == true and web.module_raycast_enabled == false)
    check(web.rendering_device == false and web.builtin_pcre2_with_jit == false)
    local custom = resolve({platform = "web", arch = "wasm32", javascript_eval = "n", wasm_simd = "n", optimize = "speed", initial_memory = "128"})
    check(custom.javascript_eval == false and custom.wasm_simd == false and custom.optimize == "speed" and custom.initial_memory == 128)
    local system_theora = resolve({platform = "linuxbsd", builtin_libtheora = "n", builtin_libvorbis = true, builtin_libogg = true})
    check(system_theora.builtin_libvorbis == false and system_theora.builtin_libogg == false)
    local system_vorbis = resolve({platform = "linuxbsd", builtin_libvorbis = "n", builtin_libogg = true})
    check(system_vorbis.builtin_libtheora == true and system_vorbis.builtin_libogg == false)
    local windows_codec = resolve({platform = "windows", builtin_libtheora = "n", builtin_libvorbis = true, builtin_libogg = true})
    check(windows_codec.builtin_libvorbis == true and windows_codec.builtin_libogg == true)
    local invalid = utils.trycall(function() resolve({platform = "linuxbsd", arch = "x86_64", wayland = "maybe"}) end)
    check(not invalid)
    invalid = utils.trycall(function() resolve({platform = "web", arch = "wasm32", initial_memory = "nan"}) end)
    check(not invalid)
    local mono = import("build.xmake.recipes.modules.mono.config", {rootdir = root})
    for _, platform in ipairs({"windows", "linuxbsd", "macos", "android", "ios", "visionos", "web"}) do
        local _, env = resolve({platform = platform, module_mono_enabled = "n"})
        check(table.contains(env.supported, "mono") == (platform ~= "web"))
        check(mono.can_build(env, platform) == (platform ~= "web"))
        if platform ~= "web" then
            local graph = model.new(root, {platform = platform, module_mono_enabled = "y"})
            graph:configure()
            check(graph.options.module_mono_enabled == true and graph.environment.module_list.mono ~= nil)
        end
    end
    local success, message = utils.trycall(function()
        model.new(root, {platform = "web", module_mono_enabled = "y"}):configure()
    end)
    check(not success and tostring(message):find("module_mono_enabled is unsupported on web", 1, true) ~= nil)
    print("NATIVE_PLATFORM_DEFAULT_CHECKS=" .. checks)
end
