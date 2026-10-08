-- Whole-module native Swift compilation with an Objective-C interoperability header.
function arguments(settings, options, sdk, object, header)
    local flags = {"-wmo", "-warnings-as-errors", "-cxx-interoperability-mode=default", "-emit-object",
        "-emit-objc-header-path", header, "-target", settings.triple, "-sdk", sdk,
        "-import-objc-header", settings.bridge, "-swift-version", "6", "-parse-as-library",
        "-module-name", settings.module, "-I", settings.root, "-o", object}
    if options.debug_symbols then table.insert(flags, "-g") end
    local optimize = options.optimize
    if optimize == "speed" or optimize == "speed_trace" then table.insert(flags, "-O")
    elseif optimize == "size" then table.insert(flags, "-Osize")
    else table.insert(flags, "-Onone") end
    if options.osxcross and options.osxcross ~= "" then
        local resource = options.SWIFT_RESOURCE_DIR or os.getenv("SWIFT_RESOURCE_DIR")
        assert(resource and resource ~= "", "Swift cross compilation requires SWIFT_RESOURCE_DIR")
        table.join2(flags, {"-resource-dir", resource, "-Xfrontend", "-enable-cross-import-overlays"})
    end
    return flags
end

function configure(target, settings, options)
    if not settings then return end
    assert(options.platform == "ios" or options.platform == "visionos", "Unexpected Swift platform policy")
    import("core.project.project")
    target:rule_add(assert(project.rule("egp.apple.swift"), "Native Swift module rule is not registered"))
    target:add("rules", "egp.apple.swift", {override = true})
    target:data_set("egp.apple.swift.settings", settings)
    target:data_set("egp.apple.swift.options", options)
    target:add("includedirs", target:autogendir("egp.apple.swift"), {public = true})
end

function prepare(target, sourcebatch, opt)
    local settings = assert(target:data("egp.apple.swift.settings"))
    local options = assert(target:data("egp.apple.swift.options"))
    local object = assert(target:data("egp.apple.swift.object"))
    local header = assert(target:data("egp.apple.swift.header"))
    local compiler = settings.compiler
    if not compiler or compiler == "" then compiler = target:tool("sc") end
    assert(compiler and compiler ~= "", "Native Swift compiler is required for Apple embedded targets")
    local sdk = settings.sdk
    if not sdk or sdk == "" then
        assert(os.host() == "macosx", "Cross compilation requires an explicit APPLE_SDK_PATH")
        sdk = assert(os.iorunv("xcrun", {"--sdk", settings.sdk_name, "--show-sdk-path"})):trim()
    end
    assert(os.isdir(sdk), "Apple SDK directory does not exist: " .. sdk)
    local flags = arguments(settings, options, sdk, object, header)
    local files = table.join(sourcebatch.sourcefiles, {settings.bridge})
    -- Bridging headers import local C++/Objective-C interfaces; conservatively track their headers.
    for _, folder in ipairs({"core", "drivers/apple_embedded", "platform/" .. options.platform}) do
        table.join2(files, os.files(path.join(settings.root, folder, "**.h")))
    end
    local depend = import("core.project.depend")
    depend.on_changed(function()
        os.mkdir(path.directory(object))
        os.mkdir(path.directory(header))
        os.vrunv(compiler, table.join(sourcebatch.sourcefiles, flags), {timeout = 300000})
        assert(os.isfile(object) and os.isfile(header), "Swift compiler omitted module object or Objective-C header")
    end, {files = files, values = table.join({compiler}, flags), dependfile = target:dependfile(object),
        changed = target:is_rebuilt() or not os.isfile(object) or not os.isfile(header)})
end
