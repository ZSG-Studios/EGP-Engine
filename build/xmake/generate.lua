local json = import("core.base.json")
local bytes = import("core.base.bytes")

local function canonical(value)
    if type(value) ~= "table" then return json.encode(value) end
    local keys, parts = table.keys(value), {}
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, key in ipairs(keys) do table.insert(parts, json.encode(tostring(key)) .. ":" .. canonical(value[key])) end
    return "{" .. table.concat(parts, ",") .. "}"
end

local function host_tools(root, options)
    options = options or {}
    local project = path.join(root, "build/xmake/tools")
    local directory = path.join(root, ".build/xmake-host-tools", os.host() .. "-" .. os.arch())
    local executable = path.join(directory, "bin/egp_compress" .. (os.host() == "windows" and ".exe" or ""))
    os.mkdir(directory)
    local envs = {XMAKE_CONFIGDIR = path.join(directory, "config"), XMAKE_GLOBALDIR = path.join(root, ".build/xmake-global/codegen")}
    local hosts = import("platforms.host", {rootdir=os.scriptdir()})
    local host = hosts.select(options)
    local configure = {"f", "-y", "--toolchain=" .. host.toolchain, "-P", project, "-o", directory, "-p", host.plat, "-a", host.arch, "-m", "release"}
    -- The built-in xmake SDK option is not an engine graph option.
    local sdkroot = options.mingw or import("core.project.config").get("mingw")
    table.join2(configure, hosts.configure_arguments(host, options, sdkroot))
    os.vrunv(os.programfile(), configure, {curdir = project, envs = envs})
    os.vrunv(os.programfile(), {"-P", project, "-b", "-j", "4"}, {curdir = project, envs = envs})
    assert(os.isfile(executable), "Native host compressor build failed")
    return executable, path.join(directory, "bin/egp_zip" .. (os.host() == "windows" and ".exe" or ""))
end

function run(graph, directory)
    directory = path.absolute(directory)
    os.mkdir(directory)
    local compressor, zipper = host_tools(graph.root, graph.options)
    local context = {root = graph.root, generated = directory, compress_executable = compressor, zip_executable = zipper}
    graph.generator_context = context
    local manifest_path = path.join(directory, "manifest.json")
    local previous = os.isfile(manifest_path) and json.loadfile(manifest_path) or {}
    local manifest, outputs = {}, {}
    local implementations = {}
    for _, filename in ipairs(os.files(path.join(graph.root, "build/xmake/generators/**.lua"))) do table.insert(implementations, filename) end
    table.insert(implementations, path.join(graph.root, "version.lua"))
    table.insert(implementations, compressor)
    for _, filename in ipairs(os.files(path.join(graph.root, "servers/**.glsl"))) do table.insert(implementations, filename) end
    for _, filename in ipairs(os.files(path.join(graph.root, "drivers/**.glsl"))) do table.insert(implementations, filename) end
    table.sort(implementations)
    local implementation_hashes = {}
    for _, filename in ipairs(implementations) do table.insert(implementation_hashes, hash.sha256(filename)) end
    local implementation = table.concat(implementation_hashes)
    local options = canonical(graph.options)
    for _, generator in ipairs(graph.generators) do for _, output in ipairs(generator.outputs) do outputs[output.path] = true end end
    local count = 0
    for _, generator in ipairs(graph.generators) do
        local job = {builder = generator.generator, targets = {}, sources = {}, options = table.clone(generator.environment.options), module_order = graph.module_order}
        for _, key in ipairs({"egp_cpp_bits", "egp_cpp_api"}) do job.options[key] = generator.environment:get(key) end
        local signature_parts = {implementation, options, tostring(job.builder)}
        local complete = true
        for _, target in ipairs(generator.outputs) do
            local filename = path.join(directory, path.relative(target.path, graph.root))
            table.insert(job.targets, filename)
            complete = complete and os.isfile(filename)
        end
        for _, input in ipairs(generator.inputs) do
            if input.literal then
                table.insert(job.sources, {value = input.value, literal = true})
                table.insert(signature_parts, canonical(input.value))
            else
                local filename = outputs[input.path] and path.join(directory, path.relative(input.path, graph.root)) or input.path
                assert(os.isfile(filename), "Missing native generator input: " .. filename)
                table.insert(job.sources, {path = filename})
                table.insert(signature_parts, hash.sha256(filename))
            end
        end
        local key = path.relative(generator.outputs[1].path, graph.root):gsub("\\", "/")
        local signature = hash.sha256(bytes(table.concat(signature_parts)))
        manifest[key] = signature
        if job.builder:endswith("native_extension_sdk.build_header") then
            graph.sdk_job = job
            manifest[key] = nil
        elseif not complete or previous[key] ~= signature then
            for _, filename in ipairs(job.targets) do os.mkdir(path.directory(filename)) end
            if job.builder == "copy" then os.cp(job.sources[1].path, job.targets[1])
            elseif import("generators.shaders", {rootdir = os.scriptdir()}).generate(job, context) then
            else import("generators.init", {rootdir = os.scriptdir()}).generate(job, context) end
            for _, filename in ipairs(job.targets) do assert(os.isfile(filename), "Generator did not publish " .. filename) end
            count = count + 1
        end
    end
    json.savefile(manifest_path, manifest)
    print("Native generation: " .. count .. " groups changed, " .. (#graph.generators - count) .. " unchanged")
end

function tools(root) return host_tools(root) end
