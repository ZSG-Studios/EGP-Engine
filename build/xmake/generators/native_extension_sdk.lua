-- Deterministic offline SDK packaging; the editor's archive format is unchanged.
local bytes = import("core.base.bytes")
local json = import("core.base.json")

local function digest(data)
    if #data == 0 then return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" end
    return hash.sha256(bytes(data))
end
local function hex_bytes(hex) return (hex:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end)) end
local function u32(value)
    assert(value >= 0 and value < 4294967296, "SDK archive exceeds its format limits")
    local parts = {}
    for index = 1, 4 do parts[index] = string.char(value % 256); value = math.floor(value / 256) end
    return table.concat(parts)
end
local function names(files)
    local result = {}; for name in pairs(files) do result[#result + 1] = name end
    -- Lua's string comparison follows LC_COLLATE; archive order must follow bytes.
    table.sort(result, function(left, right)
        for index = 1, math.min(#left, #right) do
            local a, b = left:byte(index), right:byte(index)
            if a ~= b then return a < b end
        end
        return #left < #right
    end)
    return result
end
function make_archive(cpp_root, template_root, api_file, bits, precision, generation_directory)
    assert(os.isfile(api_file), "An actual engine extension API dump is required")
    local generator = import("build.xmake.generators.binding_generator", {rootdir = os.projectdir()})
    generator.main(api_file, path.join(cpp_root, "gdextension/gdextension_interface.json"), generation_directory, bits, precision)
    local files = {}
    local function collect(root, folder)
        for _, filename in ipairs(os.files(path.join(root, folder, "**"))) do
            local name = path.relative(filename, root):gsub("\\", "/")
            local generated = false
            for part in name:gmatch("[^/]+") do
                if part == ".build" or part == ".xmake" or part == ".git" then generated = true; break end
            end
            if not generated then files[name] = assert(io.readfile(filename, {encoding = "binary"})) end
        end
    end
    collect(cpp_root, "include"); collect(cpp_root, "src")
    collect(generation_directory, "gen"); collect(template_root, "")
    files["LICENSE.md"] = assert(io.readfile(path.join(cpp_root, "LICENSE.md"), {encoding = "binary"}))
    files["tools/generated_objects.lua"] = assert(io.readfile(path.join(os.projectdir(), "build/xmake/generated_objects.lua"), {encoding = "binary"}))
    local api_bytes = assert(io.readfile(api_file, {encoding = "binary"}))
    local api = json.decode(api_bytes).header
    local api_version = string.format("%.0f.%.0f", api.version_major, api.version_minor)
    files["templates/extension.gdextension.in"] = files["templates/extension.gdextension.in"]:gsub("@API@", api_version)
    files["xmake.lua"] = files["xmake.lua"]:gsub("@BITS@", bits):gsub("@PRECISION_DEFINE@", precision == "double" and '"REAL_T_IS_DOUBLE",' or "")
    local fingerprint = {}
    for _, name in ipairs(names(files)) do fingerprint[#fingerprint + 1] = name .. hex_bytes(digest(files[name])) end
    local metadata = {source_sha256 = digest(table.concat(fingerprint)), api_major = api.version_major,
        api_minor = api.version_minor, api_sha256 = digest(api_bytes), bits = bits, precision = precision,
        build_system = "xmake", xmake_version = "3.1.1", public_cpp_standard = "c++17", entrypoint = "xmake.lua"}
    local fields = {}
    for _, key in ipairs(names(metadata)) do fields[#fields + 1] = json.encode(key) .. ": " .. json.encode(metadata[key]) end
    files["sdk.json"] = "{" .. table.concat(fields, ", ") .. "}"
    local archive = {}
    for _, name in ipairs(names(files)) do
        local data = files[name]; archive[#archive + 1] = u32(#name) .. u32(#data) .. name .. data
    end
    return table.concat(archive), files
end
function main(context)
    local archive, files = make_archive(context.cpp_root, context.template_root, context.api_file,
        context.bits or "64", context.precision or "single", context.generation_directory)
    if context.extract_directory then
        for name, data in pairs(files) do
            local destination = path.join(context.extract_directory, name)
            os.mkdir(path.directory(destination)); io.writefile(destination, data, {encoding = "binary"})
        end
    end
    if context.target then
        os.mkdir(path.directory(context.target))
        local support = import("build.xmake.generators.support", {rootdir = os.projectdir()})
        local compressed = support.compress(archive, context)
        local lines = {"// Generated EGP godot-cpp SDK. Do not edit.", "#pragma once",
            'inline constexpr const char *egp_cpp_sdk_hash = "' .. digest(archive) .. '";',
            "inline constexpr int egp_cpp_sdk_size = " .. #archive .. ";",
            "inline constexpr unsigned char egp_cpp_sdk_data[] = {"}
        for offset = 1, #compressed, 32 do
            local row = {}; for index = offset, math.min(offset + 31, #compressed) do row[#row + 1] = tostring(compressed:byte(index)) end
            lines[#lines + 1] = table.concat(row, ",") .. ","
        end
        lines[#lines + 1] = "};"
        io.writefile(context.target, table.concat(lines, "\n") .. "\n", {encoding = "binary"})
    end
    return {archive_sha256 = digest(archive), archive_size = #archive, file_count = #names(files)}
end

function generate(job, context)
    local options = job.options or {}
    return main({cpp_root = path.join(context.root, "thirdparty/godot-cpp"),
        template_root = path.join(context.root, "editor/settings/gdextension/cpp_sdk"),
        api_file = assert(options.egp_cpp_api, "SDK packaging requires the actual engine API"),
        bits = tostring(options.egp_cpp_bits or "64"), precision = options.precision or "single",
        generation_directory = path.join(context.generated, "cpp-sdk-bindings"),
        target = assert(job.targets[1]), compress_executable = context.compress_executable})
end

function bootstrap_header(target, context)
    os.mkdir(path.directory(target))
    local support = import("build.xmake.generators.support", {rootdir = context.root or os.projectdir()})
    local compressed = support.compress("", context)
    local values = {}
    for index = 1, #compressed do values[index] = tostring(compressed:byte(index)) end
    local header = "// Bootstrap SDK placeholder used only for actual API export.\n#pragma once\n" ..
        'inline constexpr const char *egp_cpp_sdk_hash = "' .. digest("") .. '";\n' ..
        "inline constexpr int egp_cpp_sdk_size = 0;\n" ..
        "inline constexpr unsigned char egp_cpp_sdk_data[] = {" .. table.concat(values, ",") .. "};\n"
    if not os.isfile(target) or io.readfile(target, {encoding = "binary"}) ~= header then
        io.writefile(target, header, {encoding = "binary"})
    end
end
