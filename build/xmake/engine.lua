local config = import("core.project.config")
local json = import("core.base.json")
local platform = import("platforms.init", {rootdir = os.scriptdir()})
local graph
local generated
local descriptor
local sdk_source
local bootstrap_directory

local function absolute(value)
    if value:sub(1, 1) == "#" then return path.join(os.projectdir(), value:sub(2)) end
    return path.absolute(value, os.projectdir())
end

local function describe()
    if graph then return graph end
    local root = os.projectdir()
    local options = {}
    local defaults = json.loadfile(path.join(root, "build/xmake/defaults.json"))
    for name, value in pairs(defaults) do
        local configured = config.get(name)
        if configured ~= nil and configured ~= "auto" then
            if type(value) == "boolean" then configured = platform.enabled(configured) end
            options[name] = configured
        end
    end
    for _, directory in ipairs(os.dirs(path.join(root, "modules/*"))) do
        local name = "module_" .. path.filename(directory) .. "_enabled"
        local value = config.get(name)
        if value ~= nil and value ~= "auto" then options[name] = platform.enabled(value) end
    end
    for name in io.readfile(path.join(root, "build/xmake/options.lua")):gmatch('option%("([%w_]+)"%)') do
        if config.get(name) ~= nil and config.get(name) ~= "auto" then options[name] = config.get(name) end
    end
    options.platform = config.get("godot_platform") ~= "auto" and config.get("godot_platform") or ({windows = "windows", mingw = "windows", linux = "linuxbsd", macosx = "macos", android = "android", iphoneos = "ios", wasm = "web"})[config.plat()]
    options.arch = config.get("egp_arch") ~= "auto" and config.get("egp_arch") or ({x64 = "x86_64", x86 = "x86_32", i386 = "x86_32", ["arm64-v8a"] = "arm64", ["armeabi-v7a"] = "arm32", armv7 = "arm32"})[config.arch()] or config.arch()
    if config.get("egp_target") and config.get("egp_target") ~= "auto" then options.target = config.get("egp_target") end
    options.use_mingw = config.plat() == "mingw" or options.use_mingw
    local directory = path.absolute(config.builddir())
    os.mkdir(directory)
    generated = path.join(directory, "generated")
    local configuration = path.join(directory, "engine-options.json")
    local metadata = path.join(directory, "engine-graph.json")
    json.savefile(configuration, options)
    descriptor = import("graph", {rootdir = os.scriptdir()}).new(root, options)
    descriptor:configure()
    platform.validate_editor_host(descriptor.options)
    import("generate", {rootdir = os.scriptdir()}).run(descriptor, generated)
    graph = descriptor:serialize()
    if options.target == "editor" then
        for _, library in ipairs(graph.libraries) do
            if library.name == "editor" then
                for index = #library.sources, 1, -1 do
                    if library.sources[index].path == "editor/settings/gdextension/native_extension_editor.cpp" then sdk_source = table.remove(library.sources, index) end
                end
            end
        end
        assert(sdk_source and descriptor.sdk_job, "Editor SDK source/generation declaration missing")
        bootstrap_directory = path.join(directory, "sdk-bootstrap")
        os.mkdir(bootstrap_directory)
        import("generators.native_extension_sdk", {rootdir = os.scriptdir()}).bootstrap_header(path.join(bootstrap_directory, "editor/settings/gdextension/native_extension_sdk.gen.h"), descriptor.generator_context)
    end
    json.savefile(metadata, graph)
    return graph
end

local function defines(values)
    local result = {}
    for _, value in ipairs(values or {}) do
        table.insert(result, type(value) == "table" and value[1] .. "=" .. tostring(value[2]) or value)
    end
    return result
end

local function configure(target, policy, bootstrap)
    platform.configure(target, graph.options, policy.BUILD_ENV)
    import("platforms.swift", {rootdir = os.scriptdir()}).configure(target, policy.swift, graph.options)
    target:set("targetdir", path.join(os.projectdir(), "bin"))
    target:set("objectdir", path.join(config.builddir(), "objects", target:name()))
    import("generated_objects", {rootdir = os.scriptdir()}).configure(target, generated)
    if bootstrap then target:add("includedirs", bootstrap_directory) end
    target:add("includedirs", generated, os.projectdir())
    import("generated_headers", {rootdir = os.scriptdir()}).configure(target, graph.options, generated)
    for _, directory in ipairs(policy.CPPPATH or {}) do target:add("includedirs", absolute(directory)) end
    target:add("defines", table.unpack(defines(policy.CPPDEFINES)))
    import("source_flags", {rootdir = os.scriptdir()}).configure(target, policy)
    import("linking", {rootdir = os.scriptdir()}).flags(target, policy)
    for _, directory in ipairs(policy.LIBPATH or {}) do target:add("linkdirs", absolute(directory)) end
    import("linking", {rootdir = os.scriptdir()}).apply(target, policy, graph.options)
end

local function sources(target, list, bootstrap)
    local outputs = {}
    local output_directories = {}
    for _, generator in ipairs(graph.generators) do
        for _, output in ipairs(generator.outputs) do
            outputs[output] = true
            local directory = path.directory(output)
            while directory ~= "." and directory ~= "" do
                output_directories[directory] = true
                local parent = path.directory(directory)
                if parent == directory then break end
                directory = parent
            end
        end
    end
    for _, source in ipairs(list) do
        local policy = source.policy
        local directories = {}
        for _, directory in ipairs(policy.CPPPATH or {}) do
            local original = absolute(directory)
            local relative = path.relative(original, os.projectdir())
            if not path.is_absolute(relative) and relative ~= ".." and not relative:startswith(".." .. path.sep()) then table.insert(directories, path.join(generated, relative)) end
            table.insert(directories, original)
        end
        if bootstrap and source.path == sdk_source.path then
            table.insert(directories, 1, bootstrap_directory)
        elseif output_directories[path.directory(source.path)] then
            table.insert(directories, 1, path.join(generated, path.directory(source.path)))
        end
        if outputs[source.path] then
            table.insert(directories, path.join(os.projectdir(), path.directory(source.path)))
        end
        local flags = {}
        for _, flag in ipairs(policy.CCFLAGS or {}) do table.insert(flags, flag) end
        for _, flag in ipairs(policy.CPPFLAGS or {}) do table.insert(flags, flag) end
        local filename = path.join(outputs[source.path] and generated or os.projectdir(), source.path)
        filename = import("compile_variants", {rootdir = os.scriptdir()}).file(target, source, filename, generated)
        if policy.vendor_warnings == false then
            table.insert(flags, target:is_plat("windows") and "/WX-" or "-Wno-error")
        end
        target:add("files", filename, {includedirs = directories, defines = defines(policy.CPPDEFINES),
            force = import("source_flags", {rootdir = os.scriptdir()}).file(policy, flags), warnings = policy.vendor_warnings == false and "none" or nil,
            optimize = policy.vendor_optimize and "fast" or nil})
    end
end

function load_library(target)
    describe()
    for _, library in ipairs(graph.libraries) do
        if "egp_archive_" .. library.name == target:name() then
            configure(target, library.policy)
            sources(target, library.sources)
            target:set("targetdir", path.join(config.builddir(), "lib"))
            if library.external then
                target:set("targetdir", path.join(os.projectdir(), "bin"))
                target:set("filename", library.filename)
            end
            return
        end
    end
    target:set("kind", "phony")
end

function load_program(target, wrapper)
    describe()
    if not wrapper and target:name() ~= graph.options.target then target:set("kind", "phony"); return end
    if wrapper and graph.options.platform ~= "windows" then target:set("kind", "phony"); return end
    local program = graph.programs[wrapper and 2 or 1]
    if not program then target:set("kind", "phony"); return end
    target:set("kind", program.kind)
    configure(target, program.policy)
    sources(target, program.sources)
    import("link_dependencies", {rootdir = os.scriptdir()}).configure(target, graph, program, os.projectdir())
    target:set("filename", program.filename)
    if not wrapper then import("linking", {rootdir = os.scriptdir()}).archive_group(target, program.policy, graph.options) end
    if not wrapper then
        if graph.options.target == "editor" then
            sources(target, {sdk_source})
            target:add("deps", "engine_api")
        end
        if graph.options.platform ~= "web" or not graph.options.dlink_enabled then
            for _, library in ipairs(program.policy.LIBS or {}) do
                if library.kind == "target" then target:add("deps", "egp_archive_" .. library.name) end
            end
        end
        for _, library in ipairs(program.policy.LIBS_EXTERNAL or {}) do
            if library.kind == "target" then target:add("deps", "egp_archive_" .. library.name, {inherit = false}) end
        end
        if graph.options.platform == "windows" and #graph.programs > 1 then target:add("deps", "console_wrapper") end
        if graph.options.platform == "web" and #graph.programs > 1 then target:add("deps", "web_side_module") end
    end
end

function load_web_side(target)
    describe()
    if graph.options.platform ~= "web" or #graph.programs < 2 then target:set("kind", "phony"); return end
    local program = graph.programs[2]
    configure(target, program.policy)
    import("linking", {rootdir = os.scriptdir()}).archive_group(target, program.policy, graph.options)
    sources(target, program.sources)
    import("link_dependencies", {rootdir = os.scriptdir()}).configure(target, graph, program, os.projectdir())
    target:set("filename", program.filename)
    descriptor.environment.side_wasm = target:targetfile()
    for _, library in ipairs(program.policy.LIBS or {}) do if library.kind == "target" then target:add("deps", "egp_archive_" .. library.name) end end
end

function finish_program(target)
    if not graph or target:name() ~= graph.options.target or target:kind() == "phony" then return end
    local env, options = descriptor.environment, graph.options
    local context = table.clone(descriptor.generator_context)
    context.root, context.bin_dir = os.projectdir(), path.join(os.projectdir(), "bin")
    context.targetfile, context.options = target:targetfile(), options
    context.archive_tool = target:kind() == "static" and target:tool("ar") or nil
    context.extra_suffix, context.module_version_string = env.extra_suffix, env.module_version_string
    context.external_modules = env.MODULES_EXTERNAL or {}
    context.moltenvk_xcframework = options.moltenvk_xcframework or env.moltenvk_xcframework
    context.side_wasm = env.side_wasm
    local version = import("version", {rootdir = os.projectdir()}).info()
    context.version = version
    context.build_version = string.format("%d.%d.%d.%s", version.major, version.minor, version.patch, version.status)
    context.archives = {}
    for _, dependency in ipairs(target:orderdeps()) do
        if dependency:kind() == "static" and not dependency:name():startswith("egp_archive_external_") then table.insert(context.archives, dependency:targetfile()) end
    end
    import("platforms.package", {rootdir = os.scriptdir()}).finish(context)
end

function load_bootstrap(target)
    describe()
    if graph.options.target ~= "editor" then target:set("kind", "phony"); return end
    local program = graph.programs[1]
    configure(target, program.policy, true)
    import("linking", {rootdir = os.scriptdir()}).archive_group(target, program.policy, graph.options)
    sources(target, program.sources, true)
    sources(target, {sdk_source}, true)
    target:set("filename", program.filename:gsub("%.exe$", "") .. ".bootstrap" .. (graph.options.platform == "windows" and ".exe" or ""))
    for _, library in ipairs(program.policy.LIBS or {}) do if library.kind == "target" then target:add("deps", "egp_archive_" .. library.name) end end
end

function load_api(target)
    describe()
    if graph.options.target ~= "editor" then target:set("deps", {}); target:set("kind", "phony") end
end

function build_api(target)
    if graph.options.target ~= "editor" then return end
    local bootstrap = target:dep("editor_bootstrap"):targetfile()
    local directory = path.join(config.builddir(), "engine-api")
    os.mkdir(directory)
    local api = path.join(directory, "extension_api.json")
    local stamp_path = path.join(directory, "sdk-stamp.json")
    local implementations = os.files(path.join(os.projectdir(), "build/xmake/**.lua"))
    local fence = import("api_fence", {rootdir = os.scriptdir()})
    local signature = fence.signature(bootstrap, implementations, descriptor.sdk_job.sources, os.projectdir())
    local previous = os.isfile(stamp_path) and json.loadfile(stamp_path) or {}
    if fence.valid(previous, signature, api, descriptor.sdk_job.targets[1]) then return end
    local fresh = fence.dump_fresh(bootstrap, directory)
    descriptor.sdk_job.options.egp_cpp_api = path.absolute(fresh)
    fence.publish(fresh, api, descriptor.sdk_job.targets[1], function(prepared_header)
        local staged = table.clone(descriptor.sdk_job)
        staged.targets = table.clone(descriptor.sdk_job.targets)
        staged.targets[1] = prepared_header
        import("generators.native_extension_sdk", {rootdir = os.scriptdir()}).generate(staged, descriptor.generator_context)
    end)
    json.savefile(stamp_path, {signature = signature, api_hash = hash.sha256(api), header_hash = hash.sha256(descriptor.sdk_job.targets[1])})
end
