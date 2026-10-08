local json = import("core.base.json")

function enabled(value)
    if type(value) == "string" then
        if value == "y" or value == "yes" or value == "true" or value == "1" then return true end
        if value == "n" or value == "no" or value == "false" or value == "0" or value == "" then return false end
        raise("Invalid boolean engine option: %s", value)
    end
    return value == true
end

-- Native platform capability; this does not certify managed game export workloads.
function supports_mono(platform)
    return table.contains({"windows", "linuxbsd", "macos", "android", "ios", "visionos"}, platform)
end

function configure(env, options, explicit)
    local aliases = {linux = "linuxbsd", bsd = "linuxbsd"}
    options.platform = aliases[options.platform] or options.platform
    local platform_policy = import("platform_defaults", {rootdir = os.scriptdir()})
    local defaults = {
        target = "editor", dev_build = false, dev_mode = false, production = false,
        optimize = "auto", precision = "single", deprecated = true, threads = true,
        disable_exceptions = true, warnings = "all", werror = false, tests = false,
        strict_checks = false, use_static_cpp = true, lto = "none", rendering_device = true,
        forward_mobile_renderer = true, forward_plus_renderer = true, engine_update_check = true,
        no_editor_splash = true, use_precise_math_checks = false, limit_transitive_includes = false,
        minizip = true, brotli = true, disable_overrides = false, disable_path_overrides = true,
        extra_suffix = "", object_prefix = "", build_profile = "", library_type = "executable"
    }
    for key, value in pairs(platform_policy.get(options.platform)) do defaults[key] = value end
    for key, value in pairs(defaults) do
        if not explicit[key] then options[key] = value end
        if type(value) == "boolean" then options[key] = enabled(options[key]) end
    end
    for _, key in ipairs({"use_llvm", "use_mingw", "debug_crt", "use_asan", "use_ubsan", "use_tsan", "use_lsan", "use_msan"}) do options[key] = enabled(options[key]) end
    platform_policy.normalize(options, json.loadfile(path.join(env.graph.root, "build/xmake/defaults.json")), enabled)
    local platform, target = options.platform, options.target
    assert(table.contains({"windows", "linuxbsd", "macos", "android", "ios", "visionos", "web"}, platform), "Unsupported platform")
    assert(table.contains({"editor", "template_debug", "template_release"}, target), "Unsupported target")
    options.arch = options.arch or (platform == "web" and "wasm32" or (table.contains({"android", "ios", "visionos"}, platform) and "arm64" or "x86_64"))
    local arch_aliases = {x64 = "x86_64", amd64 = "x86_64", x86 = "x86_32", aarch64 = "arm64", riscv64 = "rv64"}
    options.arch = arch_aliases[options.arch] or options.arch
    env.editor_build, env.dev_build, env.debug_features = target == "editor", options.dev_build, target ~= "template_release"
    assert(not env.editor_build or options.library_type == "executable", "Editor SDK generation requires an executable editor; library_type static_library/shared_library is supported for template targets only")
    env.msvc = platform == "windows" and not options.use_mingw
    if options.dev_mode then
        for key, value in pairs({verbose = true, warnings = "extra", werror = true, tests = true, strict_checks = true}) do if not explicit[key] then options[key] = value end end
    end
    if options.production then
        for key, value in pairs({use_static_cpp = true, debug_symbols = false, lto = "auto"}) do if not explicit[key] then options[key] = value end end
        if platform == "android" and not explicit.swappy then options.swappy = true end
    end
    if explicit.debug_symbols then options.debug_symbols = enabled(options.debug_symbols)
    elseif not options.production then options.debug_symbols = env.dev_build end
    if options.optimize == "auto" then options.optimize = env.dev_build and "none" or (env.debug_features and "speed_trace" or "speed") end
    env.disabled_classes = {}
    if options.build_profile ~= "" then
        local profile = json.loadfile(options.build_profile)
        env.disabled_classes = profile.disabled_classes or {}
        for _, name in ipairs(env.disabled_classes) do assert(type(name) == "string", "Invalid disabled class") end
        for key, value in pairs(profile.disabled_build_options or {}) do options[key] = value end
    end
    platform_policy.normalize(options, json.loadfile(path.join(env.graph.root, "build/xmake/defaults.json")), enabled)
    -- System Theora/Vorbis must share their system Ogg dependency, as upstream does.
    if platform == "linuxbsd" then
        if options.builtin_libtheora == false then options.builtin_libvorbis, options.builtin_libogg = false, false end
        if options.builtin_libvorbis == false then options.builtin_libogg = false end
    end
    for _, key in ipairs({"disable_2d", "disable_3d", "disable_advanced_gui", "disable_physics_2d", "disable_physics_3d", "disable_navigation_2d", "disable_navigation_3d"}) do
        assert(not env.editor_build or not enabled(options[key]), "Editor cannot disable scene, GUI, physics or navigation capabilities")
    end
    if enabled(options.disable_2d) then options.disable_navigation_2d, options.disable_physics_2d, options.tests = true, true, false end
    if enabled(options.disable_3d) then options.disable_navigation_3d, options.disable_physics_3d, options.disable_xr = true, true, true end
    if platform == "web" then options.rendering_device = false end
    if not os.isfile(path.join(env.graph.root, "main/splash_editor.png")) then options.no_editor_splash = true end
    if not options.rendering_device or not (options.forward_mobile_renderer or options.forward_plus_renderer) then options.vulkan, options.metal, options.d3d12 = false, false, false end
    env.disabled_modules, env.module_dependencies, env.module_optional_dependencies = {}, {}, {}
    env.module_icons_paths, env.module_list, env.modules_detected, env.doc_class_path = {}, {}, {}, {}
    env.module_version_string = ""
    env.platform_exporters, env.platform_apis = {}, {}
    for _, filename in ipairs(os.files(path.join(env.graph.root, "platform/*/export/export.cpp"))) do table.insert(env.platform_exporters, path.filename(path.directory(path.directory(filename)))) end
    for _, filename in ipairs(os.files(path.join(env.graph.root, "platform/*/api/api.cpp"))) do table.insert(env.platform_apis, path.filename(path.directory(path.directory(filename)))) end
    table.sort(env.platform_exporters); table.sort(env.platform_apis)
    env.extra_suffix = options.extra_suffix ~= "" and "." .. options.extra_suffix or ""
    if table.contains({"ios", "visionos"}, platform) and enabled(options.simulator) then env.extra_suffix = ".simulator" .. env.extra_suffix end
    if table.contains({"windows", "linuxbsd"}, platform) and options.use_llvm then env.extra_suffix = ".llvm" .. env.extra_suffix end
    if platform == "web" and enabled(options.dlink_enabled) then env.extra_suffix = ".dlink" .. env.extra_suffix end
    for _, name in ipairs({"asan", "ubsan", "tsan", "lsan", "msan"}) do if options["use_" .. name] then env.extra_suffix = env.extra_suffix .. ".san"; break end end
    env.CC = options.compiler_cc or (env.msvc and (options.use_llvm and "clang-cl" or "cl") or (platform == "web" and "emcc" or (options.use_llvm or table.contains({"macos", "ios", "visionos", "android"}, platform)) and "clang" or "gcc"))
    env.CXX = options.compiler_cxx or (env.msvc and env.CC or (platform == "web" and "em++" or env.CC == "clang" and "clang++" or "g++"))
    env.x86_libtheora_opt_gcc, env.x86_libtheora_opt_vc = options.arch == "x86_32" and not env.msvc, options.arch == "x86_32" and env.msvc
    env.CPPPATH = {"#", "#platform/" .. platform}
    local definitions = {}
    if env.editor_build then table.insert(definitions, "TOOLS_ENABLED") end
    if env.debug_features then table.insert(definitions, "DEBUG_ENABLED") end
    table.insert(definitions, env.dev_build and "DEV_ENABLED" or "NDEBUG")
    if not options.deprecated then table.insert(definitions, "DISABLE_DEPRECATED") end
    assert(options.precision == "single" or options.precision == "double", "Invalid precision")
    if options.precision == "double" then table.insert(definitions, "REAL_T_IS_DOUBLE") end
    if options.library_type ~= "executable" then table.insert(definitions, "LIBGODOT_ENABLED") end
    if options.disable_exceptions and env.msvc then table.insert(definitions, {"_HAS_EXCEPTIONS", 0}) end
    for key, definition in pairs({threads = "THREADS_ENABLED", strict_checks = "STRICT_CHECKS", use_precise_math_checks = "PRECISE_MATH_CHECKS", rendering_device = "RD_ENABLED", minizip = "MINIZIP_ENABLED", brotli = "BROTLI_ENABLED", disable_2d = "_2D_DISABLED", disable_3d = "_3D_DISABLED", disable_advanced_gui = "ADVANCED_GUI_DISABLED", disable_physics_2d = "PHYSICS_2D_DISABLED", disable_physics_3d = "PHYSICS_3D_DISABLED", disable_navigation_2d = "NAVIGATION_2D_DISABLED", disable_navigation_3d = "NAVIGATION_3D_DISABLED", disable_xr = "XR_DISABLED"}) do
        if enabled(options[key]) then table.insert(definitions, definition) end
    end
    if options.rendering_device then
        if options.forward_mobile_renderer then table.insert(definitions, "MOBILE_RD_ENABLED") end
        if options.forward_plus_renderer then table.insert(definitions, "FORWARD_RD_ENABLED") end
    end
    if options.vulkan then table.insert(definitions, "VULKAN_ENABLED") end
    if options.use_volk then table.insert(definitions, "USE_VOLK") end
    if options.opengl3 then table.insert(definitions, "GLES3_ENABLED") end
    if env.editor_build then
        if options.engine_update_check then table.insert(definitions, "ENGINE_UPDATE_CHECK_ENABLED") end
        if options.no_editor_splash or not os.isfile(path.join(env.graph.root, "main/splash_editor.png")) then table.insert(definitions, "NO_EDITOR_SPLASH") end
    end
    if not options.disable_overrides then table.insert(definitions, "OVERRIDE_ENABLED") end
    if env.editor_build or not options.disable_path_overrides then table.insert(definitions, "OVERRIDE_PATH_ENABLED") end
    if options.optimize == "size_extra" then table.insert(definitions, "SIZE_EXTRA") end
    if options.limit_transitive_includes and not env.msvc then table.insert(definitions, "_LIBCPP_REMOVE_TRANSITIVE_INCLUDES") end
    env:add({CPPDEFINES = definitions})
    for option, field in pairs({cppdefines = "CPPDEFINES", ccflags = "CCFLAGS", cxxflags = "CXXFLAGS", cflags = "CFLAGS", linkflags = "LINKFLAGS", asflags = "ASFLAGS", arflags = "ARFLAGS", rcflags = "RCFLAGS"}) do
        if options[option] then env:add({[field] = os.argv(options[option])}) end
    end
    if env.msvc then
        env:add({CFLAGS = {"/std:c17"}, CXXFLAGS = {"/std:c++17", "/Zc:__cplusplus"}})
        if options.arch == "x86_64" then env:add({CCFLAGS = options.use_llvm and {"-msse4.2", "-mpopcnt"} or {"/d2archSSE42"}})
        elseif options.arch == "x86_32" then env:add({CCFLAGS = options.use_llvm and {"-msse2", "-mfpmath=sse", "-mstackrealign"} or {"/arch:SSE2"}}) end
        if not options.disable_exceptions then env:add({CXXFLAGS = {"/EHsc"}}) end
    else
        env:add({CFLAGS = {"-std=gnu17"}, CXXFLAGS = {"-std=gnu++17"}})
        if options.disable_exceptions then env:add({CXXFLAGS = {"-fno-exceptions"}}) end
        if options.arch == "x86_64" then env:add({CCFLAGS = {"-msse4.2", "-mpopcnt"}})
        elseif options.arch == "x86_32" then env:add({CCFLAGS = {"-msse2", "-mfpmath=sse", "-mstackrealign"}}) end
    end
    suffix(env, options)
    assert(not options.module_mono_enabled or supports_mono(platform), "module_mono_enabled is unsupported on " .. platform .. "; Web does not support the Mono module")
    env.supported = supports_mono(platform) and {"mono"} or {}
    if platform == "windows" then table.join2(env.supported, {"library", "d3d12", "dcomp", "xaudio2"}) end
    if platform == "linuxbsd" or platform == "macos" then table.insert(env.supported, "library") end
    if platform == "macos" or platform == "ios" or platform == "visionos" then table.insert(env.supported, "metal") end
end

function suffix(env, options)
    local value = "." .. options.platform .. "." .. options.target
    if env.dev_build then value = value .. ".dev" end
    if options.precision == "double" then value = value .. ".double" end
    value = value .. "." .. options.arch
    if not options.threads then value = value .. ".nothreads" end
    value = value .. env.extra_suffix .. env.module_version_string
    local extensions = {windows = ".exe", android = ".so", ios = ".a", visionos = ".a"}
    if options.library_type == "static_library" then extensions[options.platform] = env.msvc and ".lib" or ".a"
    elseif options.library_type == "shared_library" then extensions[options.platform] = options.platform == "windows" and ".dll" or options.platform == "macos" and ".dylib" or ".so" end
    env.PROGSUFFIX, env.PROGSUFFIX_WRAP = value .. (extensions[options.platform] or ""), value .. ".console.exe"
    env.OBJSUFFIX, env.LIBSUFFIX = options.platform == "windows" and ".obj" or ".o", env.msvc and ".lib" or ".a"
    env.SHLIBSUFFIX = options.platform == "windows" and ".dll" or (options.platform == "macos" and ".dylib" or ".so")
    env.LIBPREFIX, env.OBJPREFIX, env.SHOBJPREFIX = env.msvc and "" or "lib", options.object_prefix, options.object_prefix
end
