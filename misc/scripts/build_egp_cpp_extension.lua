-- Compile a real extension against the generated SDK using xmake alone.
function main(...)
    local argv, options, index = {...}, {}, 1
    while index <= #argv do
        local name, value = argv[index]:match("^%-%-([^=]+)=(.*)$")
        if not name then
            name = assert(argv[index]:match("^%-%-(.+)$"), "Expected a named option")
            index = index + 1; value = assert(argv[index], "Missing value for " .. name)
        end
        options[name] = value; index = index + 1
    end
    local project = path.absolute(assert(options.project, "--project is required"))
    local sdk = path.absolute(assert(options.sdk, "--sdk is required"))
    local build = path.absolute(assert(options["build-dir"], "--build-dir is required"))
    local output = path.absolute(assert(options["output-dir"], "--output-dir is required"))
    local basename = assert(options.name, "--name is required")
    local mode = options.mode or "debug"
    assert(mode == "debug" or mode == "release", "Unsupported extension mode")
    local platform = options.platform or os.host()
    local arch = options.arch or os.arch()
    local generated = path.join(build, "project")
    os.mkdir(generated); os.mkdir(output)
    local function quote(value) return string.format("%q", value:gsub("\\", "/")) end
    local lines = {'set_xmakever("3.0.0")', 'add_rules("mode.debug", "mode.release")',
        'option("egp_cpp_sdk") set_showmenu(true) option_end()',
        "includes(" .. quote(path.join(sdk, "xmake.lua")) .. ")",
        'target("extension")', 'set_kind("shared")', 'set_prefixname("")', 'set_languages("cxx17")',
        'if is_plat("windows") then set_runtimes(is_mode("debug") and "MDd" or "MD") end',
        "set_basename(" .. quote(basename) .. ")", "set_targetdir(" .. quote(output) .. ")",
        "add_files(" .. quote(path.join(project, options.sources or "src/**.cpp")) .. ")",
        "add_includedirs(" .. quote(path.join(project, "src")) .. ")", 'add_deps("godot-cpp")',
        'on_config(function(target) import("tools.generated_objects", {rootdir = ' .. quote(sdk) .. '}).apply(target, ' .. quote(path.join(sdk, "gen")) .. ') end)'}
    if options["doc-classes"] and mode == "debug" then
        local generator = import("tools.doc_source_generator", {rootdir = sdk})
        local doc_file = path.join(generated, "doc_data.gen.cpp")
        generator.main(doc_file, path.absolute(options["doc-classes"]), assert(options.compressor, "--compressor is required for extension documentation"))
        lines[#lines + 1] = "add_files(" .. quote(doc_file) .. ")"
    end
    lines[#lines + 1] = "target_end()"
    io.writefile(path.join(generated, "xmake.lua"), table.concat(lines, "\n") .. "\n")
    local commands = {{"f", "-y", "-P", generated, "-o", path.join(build, "native"), "-p", platform,
        "-a", arch, "-m", mode, "--egp_cpp_sdk=" .. sdk, "--egp_cpp_cache=" .. path.join(path.directory(build), "sdk-cache")},
        {"-P", generated, "-b", "-j", options.jobs or "4", "extension"}}
    local environment = {XMAKE_CONFIGDIR = path.join(build, "config"), XMAKE_GLOBALDIR = path.join(build, "global")}
    local json = import("core.base.json")
    local receipt = path.join(build, "receipt.json")
    local result = {passed = false, sdk = json.decode(io.readfile(path.join(sdk, "sdk.json"))), commands = commands}
    io.writefile(receipt, json.encode(result))
    for stage, arguments in ipairs(commands) do
        local logfile = path.join(build, "stage-" .. stage .. ".log")
        try {function()
            os.vrunv(os.getenv("XMAKE") or os.programfile(), arguments, {curdir = generated, envs = environment,
                timeout = 1800000, stdout = logfile, stderr = logfile})
        end, catch {function(message)
            result.failed_stage = stage; result.error = tostring(message); result.log = logfile
            io.writefile(receipt, json.encode(result))
            local output = io.readfile(logfile) or ""
            raise("xmake extension stage " .. stage .. " failed; log: " .. logfile .. "\n" .. output:sub(-16384) .. "\n" .. tostring(message))
        end}}
    end
    local suffix = platform == "windows" and ".dll" or (platform == "macosx" and ".dylib" or ".so")
    local libraries = os.files(path.join(output, basename .. suffix))
    assert(#libraries > 0, "xmake completed without the requested extension library")
    local checksums = {}; for _, filename in ipairs(libraries) do checksums[filename] = hash.sha256(filename) end
    result.passed = true; result.libraries = checksums
    io.writefile(receipt, json.encode(result))
    print(receipt)
end
