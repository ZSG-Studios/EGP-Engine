local version = import("version", {rootdir = os.projectdir()})

function bind(graph)
    local helpers = {}
    helpers.get_version_info = function (suffix)
        local result = version.info()
        result.build = os.getenv("BUILD_NAME") or "custom_build"
        result.status = os.getenv("GODOT_VERSION_STATUS") or result.status
        result.module_config = result.module_config .. (suffix or "")
        return result
    end
    helpers.get_git_info = function ()
        local result = {git_hash = "", git_timestamp = 0}
        if os.isdir(path.join(graph.root, ".git")) or os.isfile(path.join(graph.root, ".git")) then
            local hash = try {function () return os.iorunv("git", {"rev-parse", "HEAD"}, {curdir = graph.root}) end}
            if hash then result.git_hash = hash:trim() end
            local timestamp = try {function () return os.iorunv("git", {"log", "-1", "--pretty=format:%ct", "--no-show-signature"}, {curdir = graph.root}) end}
            if timestamp then result.git_timestamp = tonumber(timestamp:trim()) or 0 end
        end
        return result
    end
    helpers.using_gcc = function (env) return env.CC == "gcc" or env.CC:endswith("gcc") end
    helpers.using_clang = function (env) return env.options.use_llvm or env.CC:find("clang", 1, true) ~= nil end
    helpers.using_emcc = function (env) return env.options.platform == "web" end
    helpers.get_compiler_version = function (env)
        local output = os.iorunv(env.CC, {"--version"})
        local major, minor, patch = output:match("(%d+)%.(%d+)%.(%d+)")
        return {major = tonumber(major) or 0, minor = tonumber(minor) or 0, patch = tonumber(patch) or 0}
    end
    helpers.print_error = function (message) raise("%s", message) end
    helpers.print_warning = function (message) wprint("%s", message) end
    helpers.print_info = function (message) print(message) end
    helpers.redirect_emitter = false -- Native xmake objectdir controls output placement.
    helpers.setup_swift_builder = function (env, apple_platform, sdk_path, current_path, bridging_header_filename, all_swift_files)
        local platform = env.options.platform
        assert(platform == "ios" or platform == "visionos", "Swift engine metadata requires an Apple embedded platform")
        local simulator = env.options.simulator == true
        local suffix = platform == "ios" and "ios15.0" or "xros26.0"
        if simulator then suffix = suffix .. "-simulator" end
        env.swift = {
            module = "godot_swift_module", header = "godot_swift_module-Swift.gen.h",
            bridge = path.join(current_path, bridging_header_filename),
            triple = env.options.arch .. "-apple-" .. suffix,
            sdk = type(sdk_path) == "string" and sdk_path or "",
            sdk_name = platform == "ios" and (simulator and "iphonesimulator" or "iphoneos") or (simulator and "xrsimulator" or "xros"),
            compiler = env.options.SWIFT_COMPILER or "", root = graph.root
        }
        assert(os.isfile(env.swift.bridge), "Swift bridging header missing: " .. env.swift.bridge)
        local sources = {}
        for _, filename in ipairs(all_swift_files) do
            local source = path.absolute(filename, current_path)
            assert(os.isfile(source), "Swift source missing: " .. source)
            table.insert(sources, env:file(source))
        end
        assert(#sources > 0, "Apple embedded Swift module has no sources")
        return sources
    end
    helpers.mono_configure = function (env, env_mono)
        if env.editor_build then
            assert(table.contains({"windows", "macos", "linuxbsd"}, env.options.platform), "Managed editor is supported on desktop platforms")
            env_mono:add({CPPDEFINES = {"GD_MONO_HOT_RELOAD"}})
        end
    end
    return helpers
end
