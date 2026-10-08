-- EGP platform policy. Engine sources and generation belong to the root graph.
local platforms = {windows = "windows", linuxbsd = "linux", macos = "macosx", android = "android", ios = "iphoneos", visionos = "cross", web = "wasm"}
local arches = {windows = {x86_64 = "x64", x86_32 = "x86", arm32 = "arm"}, android = {x86_64 = "x86_64", x86_32 = "x86", arm32 = "armeabi-v7a", arm64 = "arm64-v8a"}, linuxbsd = {x86_32 = "i386", arm32 = "armv7", rv64 = "riscv64", loongarch64 = "loong64"}}

function enabled(value, default)
    if value == nil then return default or false end
    return value == true or value == "yes" or value == "true" or value == "y" or value == "1"
end

function normalize(options)
    local godot = options.godot_platform or options.platform
    assert(platforms[godot], "Unknown EGP platform: " .. tostring(godot))
    local arch = options.arch or options.egp_arch or ((godot == "android" or godot == "ios" or godot == "visionos") and "arm64" or (godot == "web" and "wasm32" or "x86_64"))
    arch = ({x64="x86_64",amd64="x86_64",x86="x86_32",i386="x86_32",aarch64="arm64",riscv64="rv64"})[arch] or arch
    local toolchain = godot == "windows" and (enabled(options.use_mingw) and "mingw" or (enabled(options.use_llvm) and "clang-cl" or "msvc"))
        or (godot == "linuxbsd" and (enabled(options.use_llvm) and "clang" or "gcc"))
        or ((godot == "macos" or godot == "ios") and "xcode")
        or (godot == "android" and "ndk") or (godot == "web" and "emcc") or "egp-visionos"
    local native = godot == "linuxbsd" and os.host() == "bsd" and "bsd" or platforms[godot]
    return {plat = enabled(options.use_mingw) and godot == "windows" and "mingw" or native, arch = (arches[godot] or {})[arch] or arch, toolchain = toolchain, godot_platform = godot}
end

function configure_toolchain(target, options, build_env)
    local normalized = normalize(options)
    local settings = {}
    if normalized.godot_platform == "macos" then
        -- Xcode's target triple must agree with the compile/link deployment flags.
        settings.target_minver = normalized.arch == "arm64" and "13.0" or "11.0"
    end
    if normalized.godot_platform == "ios" or normalized.godot_platform == "visionos" then settings.simulator = enabled(options.simulator) end
    if normalized.godot_platform == "ios" then
        settings.appledev = settings.simulator and "simulator" or "iphone"
        settings.target_minver = "15.0"
    end
    if normalized.godot_platform == "web" and build_env and build_env.EMCC_CLOSURE_ARGS then
        settings.egp_closure_args = build_env.EMCC_CLOSURE_ARGS
    end
    target:set("toolchains", normalized.toolchain, settings)
    if settings.egp_closure_args then
        import("build_environment", {rootdir = os.scriptdir()}).configure(target, normalized.toolchain, settings.egp_closure_args)
    end
    return normalized
end

function validate_editor_host(options, host, host_arch)
    if options.target ~= "editor" then return end
    host, host_arch = host or os.host(), host_arch or os.arch()
    local aliases = {x64 = "x86_64", x86 = "x86_32", i386 = "x86_32", amd64 = "x86_64", aarch64 = "arm64"}
    host_arch = aliases[host_arch] or host_arch
    local expected = ({windows = "windows", linuxbsd = "linux", macos = "macosx"})[options.platform]
    local same_platform = expected == host or (options.platform == "linuxbsd" and host == "bsd")
    local runnable_arch = options.arch == host_arch
        or (host == "windows" and host_arch == "x86_64" and options.arch == "x86_32")
        or (host == "macosx" and host_arch == "arm64" and options.arch == "x86_64")
    assert(same_platform and runnable_arch, "Editor SDK bootstrap must run on its build host: use a native Windows/Linux/macOS editor and matching host architecture (macOS x86_64 on arm64 requires Rosetta). Cross-platform/foreign-architecture editor SDK bootstrap is not implemented; cross-compile template_debug/template_release instead.")
end

local function addflags(target, key, ...)
    local values = {...}
    table.insert(values, {force = true})
    target:add(key, table.unpack(values))
end
local function cc(target, ...)
    addflags(target, "cxflags", ...)
end
local function link(target, ...)
    addflags(target, "ldflags", ...)
    addflags(target, "shflags", ...)
end
local function flags(target, ...)
    cc(target, ...)
    link(target, ...)
end
local function sdkpath(value, fallback)
    return value and value ~= "" and value or fallback
end

local function moltenvk_directory(options, arch, sdk_home)
    local function version(value)
        local parts = {}; for part in value:gmatch("[^.]+") do table.insert(parts, tonumber(part) or 0) end
        return parts
    end
    local function newer(left, right)
        for index = 1, math.max(#left, #right) do
            if (left[index] or 0) ~= (right[index] or 0) then return (left[index] or 0) > (right[index] or 0) end
        end
        return false
    end
    for _, variant in ipairs({"macos-arm64_x86_64", "macos-" .. arch}) do
        local candidates = {}
        if options.vulkan_sdk_path and options.vulkan_sdk_path ~= "" then
            local sdk = path.translate(options.vulkan_sdk_path)
            table.join2(candidates, {path.join(sdk, "MoltenVK/MoltenVK.xcframework"), path.join(sdk, "macOS/lib/MoltenVK.xcframework"), sdk})
        end
        local latest, latest_version
        for _, sdk in ipairs(os.dirs(path.join(sdk_home or path.translate("~"), "VulkanSDK/*"))) do
            local sdk_version = version(path.filename(sdk))
            if not newer({1, 3, 231, 0}, sdk_version) and (not latest_version or newer(sdk_version, latest_version)) then
                for _, relative in ipairs({"macOS/lib/MoltenVK.xcframework", "MoltenVK/MoltenVK.xcframework"}) do
                    local framework = path.join(sdk, relative)
                    if os.isfile(path.join(framework, variant, "libMoltenVK.a")) then latest, latest_version = framework, sdk_version; break end
                end
            end
        end
        if latest then table.insert(candidates, latest) end
        table.join2(candidates, {"/opt/homebrew/Frameworks/MoltenVK.xcframework", "/usr/local/homebrew/Frameworks/MoltenVK.xcframework", "/opt/local/Frameworks/MoltenVK.xcframework"})
        for _, framework in ipairs(candidates) do
            local directory = path.join(framework, variant)
            if os.isfile(path.join(directory, "libMoltenVK.a")) then return directory end
        end
    end
    raise("MoltenVK SDK installation directory not found; install the Vulkan SDK or configure vulkan_sdk_path (or vulkan=n)")
end

function configure_macos_vulkan(target, options, sdk_home)
    target:add("frameworks", "Metal")
    if not enabled(options.use_volk, true) then
        target:add("linkdirs", moltenvk_directory(options, options.arch, sdk_home))
        target:add("syslinks", "MoltenVK")
    end
end

function configure(target, options, build_env)
    local sdk_paths = import("build.xmake.sdk_paths", {rootdir=path.absolute("../../..", os.scriptdir())})
    sdk_paths.resolve(options, os.projectdir())
    local normalized = configure_toolchain(target, options, build_env)
    if options.library_type == "shared_library" and normalized.plat ~= "windows" and normalized.plat ~= "mingw" then
        target:add("cxflags", "-fPIC", {force = true})
    end
    local platform = normalized.godot_platform
    local msvc = platform == "windows" and not enabled(options.use_mingw)
    if (normalized.toolchain=="gcc" or normalized.toolchain=="mingw") and target.script then
        import("compiler_warnings",{rootdir=os.scriptdir()}).configure(target)
    end
    target:add("includedirs", "platform/" .. platform)
    target:set("languages", "c17", "cxx17")
    target:set("symbols", enabled(options.debug_symbols, true) and "debug" or "none")
    local optimize = options.optimize or (enabled(options.dev_build) and "none" or "speed")
    target:set("optimize", ({none = "none", debug = "none", speed = "fast", speed_trace = "fast", size = "small", size_extra = "small"})[optimize] or optimize)
    if msvc then
        target:set("runtimes", enabled(options.debug_crt) and "MDd" or (enabled(options.use_static_cpp, true) and "MT" or "MD"))
        link(target, "/INCREMENTAL:NO")
        cc(target, "/utf-8", "/bigobj", "/Zc:__cplusplus", "/permissive-")
        target:set("exceptions", enabled(options.disable_exceptions, true) and "no-cxx" or "cxx")
    else
        cc(target, "-ffp-contract=off", "-fno-strict-aliasing")
        if enabled(options.disable_exceptions, true) then target:add("cxxflags", "-fno-exceptions", {force = true}) end
        if platform ~= "web" then cc(target, "-fvisibility=hidden") end
    end
    local warnings = options.warnings or "all"
    if msvc and not enabled(options.use_llvm) then
        cc(target, ({extra="/W4", all="/W3", moderate="/W2", no="/w"})[warnings] or "/W3")
        if warnings ~= "no" then
            cc(target, "/wd4100", "/wd4127", "/wd4201", "/wd4244", "/wd4245", "/wd4267", "/wd4305", "/wd4324", "/wd4514", "/wd4714", "/wd4820")
            if warnings == "all" then cc(target, "/w34458") end
        else cc(target, "/wd4267") end
        if enabled(options.werror) then cc(target, "/WX"); link(target, "/WX") end
    else
        cc(target, warnings == "no" and "-w" or (msvc and "-W3" or "-Wall"))
        local clang = normalized.toolchain ~= "gcc" and normalized.toolchain ~= "mingw"
        if warnings ~= "no" then
            if clang then cc(target, "-Wshadow-field-in-constructor", "-Wshadow-uncaptured-local", "-Wno-ordered-compare-function-pointers", "-Wenum-conversion")
            else cc(target, "-Wshadow", "-Wno-misleading-indentation") end
            if warnings == "extra" then
                cc(target, "-Wextra", "-Wwrite-strings", "-Wno-unused-parameter")
                target:add("cxxflags", "-Wctor-dtor-privacy", "-Wnon-virtual-dtor", {force=true})
                if clang then cc(target, "-Wimplicit-fallthrough")
                else cc(target, "-Walloc-zero", "-Wduplicated-branches", "-Wduplicated-cond", "-Wstringop-overflow=4") end
            elseif warnings == "moderate" then cc(target, "-Wno-unused") end
        end
        if enabled(options.werror) then
            cc(target, "-Werror")
            if not msvc then
                if platform ~= "macos" then link(target, "-Wl,--fatal-warnings")
                elseif normalized.arch ~= "x86_64" then link(target, "-Wl,-fatal_warnings") end
            end
        end
    end
    local sanitizer = {}
    for key, value in pairs({asan = "address", ubsan = "undefined", tsan = "thread", lsan = "leak", msan = "memory"}) do
        if enabled(options["use_" .. key]) then
            assert(not msvc or key == "asan", "MSVC only supports the address sanitizer")
            table.insert(sanitizer, value)
            target:add("defines", key:upper() .. "_ENABLED")
        end
    end
    assert(not (enabled(options.use_tsan) and enabled(options.use_asan)), "ThreadSanitizer and AddressSanitizer cannot be combined")
    if #sanitizer > 0 then
        -- GCC's sanitizer globals can exceed the x86-64 small-data range.
        -- Match the upstream Linux policy for both code generation and final links.
        if platform == "linuxbsd" and normalized.toolchain == "gcc" and normalized.arch == "x86_64" then
            flags(target, "-mcmodel=medium")
        end
        if msvc then cc(target, "/fsanitize=address"); link(target, "/INFERASANLIBS")
        else flags(target, "-fsanitize=" .. table.concat(sanitizer, ",")); cc(target, "-fno-omit-frame-pointer") end
    end
    if options.lto and options.lto ~= "none" and options.lto ~= "no" then
        if msvc and not enabled(options.use_llvm) then cc(target, "/GL"); link(target, "/LTCG")
        else flags(target, options.lto == "thin" and "-flto=thin" or "-flto") end
    end
    if options.linker and options.linker ~= "default" then link(target, "-fuse-ld=" .. options.linker) end
    if enabled(options.use_coverage) then flags(target, "--coverage") end
    if platform == "windows" then
        target:add("defines", "WINDOWS_ENABLED", "WASAPI_ENABLED", "WINMIDI_ENABLED", "NOMINMAX", "WINVER=0x0A00", "_WIN32_WINNT=0x0A00")
        if msvc then target:add("defines", "TYPED_METHOD_BIND", "WIN32") end
        if normalized.arch == "x64" or normalized.arch == "arm64" then target:add("defines", "_WIN64") end
        target:add("syslinks", "winmm", "dsound", "kernel32", "ole32", "oleaut32", "sapi", "user32", "gdi32", "iphlpapi", "shlwapi", "shcore", "wsock32", "ws2_32", "shell32", "advapi32", "dinput8", "dxguid", "imm32", "bcrypt", "crypt32", "avrt", "dwmapi", "dwrite", "wbemuuid", "ntdll", "hid", "mincore", "psapi", "dbghelp")
        if enabled(options.use_mingw) then target:add("defines", "MINGW_ENABLED", "MINGW_HAS_SECURE_API=1"); target:add("syslinks", "mingw32") end
        if enabled(options.use_static_cpp, true) and not msvc then link(target, "-static-libgcc", "-static-libstdc++") end
        if enabled(options.d3d12) then target:add("defines", "D3D12_ENABLED"); target:add("syslinks", "dxgi", "dxguid", "version") end
        if options.windows_subsystem == "console" then
            target:add("defines", "WINDOWS_SUBSYSTEM_CONSOLE")
            link(target, msvc and "/SUBSYSTEM:CONSOLE" or "-mconsole")
        else link(target, msvc and "/SUBSYSTEM:WINDOWS" or "-mwindows") end
    elseif platform == "linuxbsd" then
        target:add("defines", "LINUXBSD_ENABLED", "UNIX_ENABLED", "_FILE_OFFSET_BITS=64")
        if os.host() ~= "bsd" or enabled(options.execinfo) then target:add("defines", "CRASH_HANDLER_ENABLED") end
        if enabled(options.execinfo) then target:add("syslinks", "execinfo") end
        target:add("syslinks", "pthread", "m")
        if normalized.plat ~= "bsd" then target:add("syslinks", "dl") end
        if normalized.plat == "bsd" and os.subhost() == "freebsd" then target:add("syslinks", "kvm") end
        flags(target, "-pthread")
        if enabled(options.use_sowrap, true) then
            target:add("defines", "SOWRAP_ENABLED")
            target:add("includedirs", "thirdparty/linuxbsd_headers")
            if enabled(options.wayland, true) then target:add("includedirs", "thirdparty/linuxbsd_headers/wayland", "thirdparty/linuxbsd_headers/libdecor-0") end
        end
        if enabled(options.x11, true) then target:add("defines", "X11_ENABLED", "XKB_ENABLED") end
        if enabled(options.wayland, true) then target:add("defines", "WAYLAND_ENABLED", "XKB_ENABLED", "LIBDECOR_ENABLED"); target:add("syslinks", "rt") end
        for option, defines in pairs({alsa = {"ALSA_ENABLED", "ALSAMIDI_ENABLED"}, pulseaudio = {"PULSEAUDIO_ENABLED", "_REENTRANT"}, dbus = {"DBUS_ENABLED"}, speechd = {"SPEECHD_ENABLED"}, fontconfig = {"FONTCONFIG_ENABLED"}, udev = {"UDEV_ENABLED"}}) do
            if enabled(options[option], true) then target:add("defines", table.unpack(defines)) end
        end
        if enabled(options.use_static_cpp, true) then link(target, "-static-libgcc", "-static-libstdc++") end
    elseif platform == "macos" then
        target:add("defines", "MACOS_ENABLED", "UNIX_ENABLED", "COREAUDIO_ENABLED", "COREMIDI_ENABLED")
        target:add("frameworks", "Cocoa", "Carbon", "AudioUnit", "CoreAudio", "CoreMIDI", "IOKit", "GameController", "CoreHaptics", "CoreVideo", "AVFoundation", "CoreMedia", "QuartzCore", "Security", "UniformTypeIdentifiers", "IOSurface", "ForceFeedback")
        flags(target, normalized.arch == "arm64" and "-mmacosx-version-min=13.0" or "-mmacosx-version-min=11.0")
        target:add("mflags", "-fobjc-arc", "-fblocks")
        target:add("mxflags", "-fobjc-arc", "-fblocks")
    elseif platform == "ios" or platform == "visionos" then
        target:add("defines", platform == "ios" and "IOS_ENABLED" or "VISIONOS_ENABLED", "APPLE_EMBEDDED_ENABLED", "UNIX_ENABLED", "COREAUDIO_ENABLED")
        assert(not enabled(options.vulkan) or platform == "ios", "visionOS does not support Vulkan")
        assert(not enabled(options.opengl3) or platform == "ios", "visionOS does not support OpenGL")
        if enabled(options.simulator) then target:add("defines", platform == "ios" and "IOS_SIMULATOR" or "VISIONOS_SIMULATOR") end
        -- The custom visionOS toolchain already selects deployment via its target triple.
        -- Clang rejects a second -mtargetos alongside that explicit -target.
        if platform == "ios" then
            flags(target, enabled(options.simulator) and "-mios-simulator-version-min=15.0" or "-miphoneos-version-min=15.0")
            if enabled(options.opengl3, true) then target:add("defines", "GLES_SILENCE_DEPRECATION") end
        end
        target:add("mflags", "-fobjc-arc", "-fblocks")
        target:add("mxflags", "-fobjc-arc", "-fblocks")
    elseif platform == "android" then
        target:add("defines", "ANDROID_ENABLED", "UNIX_ENABLED", "_FILE_OFFSET_BITS=64")
        target:add("syslinks", "OpenSLES", "EGL", "android", "log", "z", "dl")
        cc(target, "-fPIC", "-ffunction-sections", "-funwind-tables", "-fstack-protector-strong")
        link(target, "-Wl,--gc-sections", "-Wl,--no-undefined", "-Wl,-z,now", "-Wl,--build-id", "-Wl,-soname,libgodot_android.so")
        if options.arch == "arm32" then cc(target, "-march=armv7-a", "-mfloat-abi=softfp"); target:add("defines", "__ARM_ARCH_7__", "__ARM_ARCH_7A__", "__ARM_NEON__") end
        if enabled(options.opengl3, true) then target:add("syslinks", "GLESv3") end
    elseif platform == "web" then
        target:add("defines", "WEB_ENABLED", "UNIX_ENABLED", "UNIX_SOCKET_UNAVAILABLE", "GDSCRIPT_NO_LSP")
        link(target, "-sINITIAL_MEMORY=" .. tostring(options.initial_memory or 32) .. "MB", "-sSTACK_SIZE=" .. tostring(options.stack_size or 5120) .. "KB", "-sENVIRONMENT=web,worker", "-sALLOW_MEMORY_GROWTH=1", "-sINVOKE_RUN=0", "-sEXIT_RUNTIME=1", "-sSUPPORT_LONGJMP=wasm", "-sMAX_WEBGL_VERSION=2", "-sGL_ENABLE_GET_PROC_ADDRESS=0", "-sOFFSCREEN_FRAMEBUFFER=1", "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']", "-sEXPORTED_RUNTIME_METHODS=['callMain','cwrap']")
        cc(target, "-sSUPPORT_LONGJMP=wasm")
        if enabled(options.threads, true) then
            target:add("defines", "PTHREAD_NO_RENAME", "PROXY_TO_PTHREAD_ENABLED")
            flags(target, "-pthread")
            link(target, "-sPROXY_TO_PTHREAD=1", "-sPTHREAD_POOL_SIZE=8", "-sMAXIMUM_MEMORY=2048MB")
        end
        if enabled(options.use_closure_compiler) then link(target, "--closure=1") end
    end
    local arch = options.arch or options.egp_arch or (platform == "android" and "arm64" or "x86_64")
    local deps = sdk_paths.dependencies(os.projectdir())
    if enabled(options.accesskit) then
        local sdk = sdkpath(options.accesskit_sdk_path, path.join(deps, "accesskit"))
        assert(os.isdir(path.join(sdk, "include")), "AccessKit SDK missing; install it or configure accesskit=n")
        target:add("includedirs", path.join(sdk, "include"))
        target:add("defines", "ACCESSKIT_ENABLED")
        local subarch = arch == "x86_32" and "x86" or arch
        if platform == "windows" then
            local compiler = msvc and "msvc" or (enabled(options.use_llvm) and "mingw-llvm" or "mingw")
            target:add("linkdirs", path.join(sdk, "lib", "windows", subarch, compiler, "static"))
            target:add("syslinks", "accesskit", "runtimeobject", "propsys", "userenv")
        elseif platform == "linuxbsd" or platform == "macos" then
            target:add("linkdirs", path.join(sdk, "lib", platform == "linuxbsd" and "linux" or "macos", subarch, "static"))
            target:add("syslinks", "accesskit")
        elseif platform == "android" then
            target:add("linkdirs", path.join(sdk, "lib", "android", normalized.arch, "static"))
            target:add("syslinks", "accesskit")
        elseif platform == "ios" then
            assert(os.isdir(path.join(sdk, "lib", "ios", "AccessKit.xcframework")), "AccessKit iOS XCFramework missing")
        end
    end
    if platform == "windows" and enabled(options.d3d12) then
        local compiler = msvc and "msvc" or (enabled(options.use_llvm) and "llvm" or "gcc")
        local mesa = options.mesa_libs
        assert(os.isdir(mesa), "Direct3D12 requires the installed Mesa/NIR SDK; install it or configure d3d12=n")
        target:add("linkdirs", path.join(mesa, "bin"))
        target:add("syslinks", "libNIR.windows." .. arch .. (msvc and enabled(options.use_asan) and ".san" or ""))
        if enabled(options.use_pix) then
            local pix = sdkpath(options.pix_path, path.join(deps, "pix"))
            assert(os.isdir(pix), "PIX SDK missing")
            target:add("linkdirs", path.join(pix, "bin", arch == "arm64" and "arm64" or "x64"))
            target:add("syslinks", "WinPixEventRuntime")
        end
    end
    if (platform == "windows" or platform == "macos") and enabled(options.angle) and enabled(options.opengl3, true) then
        local angle = sdkpath(options.angle_libs, path.join(deps, "angle"))
        local compiler = platform == "macos" and "macos" or (msvc and "msvc" or (enabled(options.use_llvm) and "llvm" or "gcc"))
        local variant = angle .. "-" .. arch .. "-" .. compiler
        if os.isdir(variant) then angle = variant end
        assert(os.isdir(angle), "ANGLE SDK missing; install it or configure angle=n")
        target:add("includedirs", "thirdparty/angle/include")
        target:add("defines", "ANGLE_ENABLED", "EGL_STATIC")
        target:add("linkdirs", angle)
        local prefix = platform == "windows" and "lib" or ""
        for _, name in ipairs({"ANGLE", "EGL", "GLES"}) do target:add("syslinks", prefix .. name .. "." .. platform .. "." .. arch .. (msvc and enabled(options.use_asan) and ".san" or "")) end
        if platform == "windows" then target:add("syslinks", "dxgi", "d3d9", "d3d11") else target:add("frameworks", "Metal") end
    end
    if platform == "android" and enabled(options.swappy) then
        local directory = path.join("thirdparty/swappy-frame-pacing", normalized.arch)
        assert(os.isfile(path.join(directory, "libswappy_static.a")), "Swappy archive missing for " .. normalized.arch)
        target:add("linkdirs", directory)
        target:add("syslinks", "swappy_static")
        target:add("defines", "SWAPPY_FRAME_PACING_ENABLED")
    end
    if platform == "linuxbsd" and not enabled(options.use_sowrap, true) then
        import("lib.detect.pkgconfig")
        local packages = {x11 = {"x11", "xcursor", "xinerama", "xext", "xrandr", "xrender", "xi"}, wayland = {"wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon", "libdecor-0"}, alsa = {"alsa"}, pulseaudio = {"libpulse"}, dbus = {"dbus-1"}, speechd = {"speech-dispatcher"}, fontconfig = {"fontconfig"}, udev = {"libudev"}}
        for option, names in pairs(packages) do
            if enabled(options[option], true) then
                for _, name in ipairs(names) do
                    local info = pkgconfig.libinfo(name)
                    assert(info, "Missing enabled system dependency: " .. name)
                    for _, key in ipairs({"includedirs", "linkdirs", "links", "cxflags"}) do if info[key] then target:add(key, table.unpack(info[key])) end end
                end
            end
        end
    end
    import("system_dependencies", {rootdir = os.scriptdir()}).configure(target, options)
    if enabled(options.sdl, platform ~= "web") then target:add("defines", "SDL_ENABLED") end
    if enabled(options.vulkan, platform == "windows" or platform == "linuxbsd" or platform == "macos" or platform == "android") then
        target:add("defines", "VULKAN_ENABLED")
        if platform == "macos" then
            configure_macos_vulkan(target, options)
        elseif not enabled(options.use_volk, true) then target:add("syslinks", platform == "windows" and "vulkan-1" or "vulkan") end
    end
    if enabled(options.opengl3, platform ~= "visionos") then target:add("defines", "GLES3_ENABLED") end
    if enabled(options.metal, platform == "macos" or platform == "ios" or platform == "visionos") and not enabled(options.simulator) then
        target:add("defines", "METAL_ENABLED")
        if platform == "macos" then target:add("frameworks", "Metal", "MetalKit", "MetalFX") end
    end
end
