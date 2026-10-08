-- Native metadata regressions: no interpreter or compiler is invoked.
function main()
    local root = os.projectdir()
    local base = path.join(root, "build/xmake/recipes")
    local recipes, configs = 0, 0
    for _, file in ipairs(os.files(path.join(base, "**/recipe.lua"))) do
        local name = (path.relative(file, base):gsub("\\", "/")):gsub("%.lua$", ""):gsub("/", ".")
        assert(type(import(name, {rootdir = base, anonymous = true}).main) == "function", "Recipe missing entry point: " .. name)
        recipes = recipes + 1
    end
    for _, file in ipairs(os.files(path.join(base, "modules/*/config.lua"))) do
        local name = (path.relative(file, base):gsub("\\", "/")):gsub("%.lua$", ""):gsub("/", ".")
        local module = import(name, {rootdir = base, anonymous = true})
        assert(type(module.can_build) == "function" and type(module.configure) == "function", "Module capability contract missing: " .. name)
        configs = configs + 1
    end
    -- The shipped catalog has 216 recipes; optional local modules can add more.
    assert(recipes >= 216 and configs >= 57, "Native metadata catalog unexpectedly incomplete")
    local model = import("build.xmake.graph", {rootdir = root})
    local packages = import("build.xmake.platform_packages", {rootdir = root})
    for _, profile in ipairs({
        {platform = "windows", arch = "x86_64", target = "editor", dev_build = true, module_mono_enabled = true},
        {platform = "linuxbsd", arch = "x86_64", target = "template_release", precision = "double", threads = false},
        {platform = "linuxbsd", arch = "x86_64", target = "template_debug", wayland = true, x11 = true},
        {platform = "macos", arch = "arm64", target = "template_debug"},
        {platform = "android", arch = "arm64", target = "template_debug"},
        {platform = "ios", arch = "arm64", target = "template_debug"},
        {platform = "ios", arch = "x86_64", target = "template_debug", simulator = true},
        {platform = "visionos", arch = "arm64", target = "template_debug"},
        {platform = "visionos", arch = "arm64", target = "template_debug", simulator = true},
        {platform = "web", arch = "wasm32", target = "template_debug"},
        {platform = "web", arch = "wasm32", target = "template_debug", dlink_enabled = true},
    }) do
        local graph = model.new(root, profile)
        packages.configure(graph)
        graph:configure()
        assert(#graph.libraries > 40 and #graph.generators > 40, "Incomplete source graph: " .. profile.platform)
        local by_name = {}
        for _, library in ipairs(graph.libraries) do by_name[library.name] = library end
        assert(by_name.core and by_name.scene and by_name.servers and by_name.modules, "Required engine archives missing")
        if profile.platform == "windows" then
            for _, source in ipairs(by_name.module_theora.sources) do
                assert(not source.path.path:find("x86_vc", 1, true), "x64 cannot compile Theora MSVC x86 inline assembly")
            end
        elseif profile.platform == "ios" or profile.platform == "visionos" then
            local program = assert(graph.programs[1])
            assert(program.kind == "static" and program.environment.swift, "Apple embedded archive missing native Swift policy")
            local count = 0
            for _, source in ipairs(program.sources) do if source.path.path:endswith(".swift") then count = count + 1 end end
            assert(count > 1, "Swift platform module must include shared cross-file declarations")
            local settings = program.environment.swift
            assert(settings.triple:endswith(profile.simulator and "-simulator" or (profile.platform == "ios" and "ios15.0" or "xros26.0")))
            assert(os.isfile(settings.bridge), "Missing Swift bridging header")
            local helper = import("build.xmake.platforms.swift", {rootdir = root})
            local flags = helper.arguments(settings, profile, "/SDK", "/module.o", "/module.h")
            assert(table.contains(flags, "-wmo") and table.contains(flags, "6"), "Whole-module Swift6 semantics lost")
        elseif profile.platform == "linuxbsd" and profile.wayland then
            local scanned = 0
            for _, job in ipairs(graph.generators) do
                if job.generator:startswith("wayland.scanner.") then
                    scanned = scanned + 1
                    assert(os.isfile(job.inputs[1].path), "Missing pinned Wayland protocol XML")
                end
            end
            assert(scanned == 56, "All 28 Wayland protocols require paired header and private-code jobs")
            for _, program in ipairs(graph.programs) do
                for _, source in ipairs(program.sources) do assert(not source.path.path:find("table:", 1, true), "Nested protocol source list was not flattened") end
            end
        elseif profile.platform == "web" then
            assert(#graph.environment.EXPORTED_RUNTIME_METHODS == 12, "Heap views must remain exported")
            local websocket = path.join(root, "modules/websocket/library_godot_websocket.js")
            local found = false
            for _, file in ipairs(graph.environment.JS_LIBS) do if file.path == websocket then found = true end end
            assert(found, "Websocket JavaScript runtime dependency lost")
            if profile.dlink_enabled then assert(#graph.programs >= 2, "Dynamic linking requires runtime and side module") end
        end
    end
    print("NATIVE_RECIPE_IMPORTS=" .. recipes .. " MODULE_CONFIG_IMPORTS=" .. configs .. " PLATFORM_PROFILES=11")
end
