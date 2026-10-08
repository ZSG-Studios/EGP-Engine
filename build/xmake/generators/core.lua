local util = import("support", {rootdir = os.scriptdir()})

function generate(name, job, context)
    local src, out = job.sources, job.targets[1]
    local value = #src > 0 and util.read(src[1], context) or nil
    local text = {}
    if name == "disabled_class_builder" then
        for _, class in ipairs(value) do
            class = class:trim()
            if #class > 0 then table.insert(text, "class " .. class .. "; template <> struct is_class_enabled<" .. class .. "> : std::false_type {};\n") end
        end
    elseif name == "version_info_builder" then
        for _, field in ipairs({"short_name", "name", "major", "minor", "patch", "status", "build", "module_config", "website", "docs_branch"}) do
            local data = value[field]
            table.insert(text, "#define GODOT_VERSION_" .. field:upper() .. " " .. (type(data) == "number" and string.format('%.0f',data) or util.cstring(data)) .. "\n")
        end
        table.insert(text, '#define GODOT_VERSION_DOCS_URL "https://docs.godotengine.org/en/" GODOT_VERSION_DOCS_BRANCH\n')
    elseif name == "version_hash_builder" then
        table.insert(text, '#include "core/version.h"\n\nconst char *const GODOT_VERSION_HASH = ' .. util.cstring(value.git_hash) .. ';\nconst unsigned long long GODOT_VERSION_TIMESTAMP = ' .. string.format('%.0f',value.git_timestamp) .. ';\n')
    elseif name == "encryption_key_builder" then
        local key = value or string.rep("0", 64)
        assert(#key == 64 and not key:find("[^0-9a-fA-F]"), "AES256 key must contain exactly 64 hexadecimal characters")
        local bytes = key:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end)
        table.insert(text, '#include <cstdint>\n\nuint8_t script_encryption_key[32] = {\n\t' .. util.buffer(bytes) .. '\n};\n')
    elseif name == "make_certs_header" then
        table.insert(text, '#define _SYSTEM_CERTS_PATH ' .. util.cstring(util.read(src[3], context) or "") .. '\n')
        if util.read(src[2], context) then
            local compressed = util.compress(value, context)
            table.insert(text, '#define BUILTIN_CERTS_ENABLED\ninline constexpr int _certs_compressed_size = ' .. #compressed .. ';\ninline constexpr int _certs_uncompressed_size = ' .. #value .. ';\ninline constexpr unsigned char _certs_compressed[] = {\n\t' .. util.buffer(compressed) .. '\n};\n')
        end
    elseif name == "make_authors_header" or name == "make_donors_header" then
        local sections = name == "make_authors_header" and {['Project Founders']='AUTHORS_FOUNDERS', ['Lead Developer']='AUTHORS_LEAD_DEVELOPERS', ['Project Manager']='AUTHORS_PROJECT_MANAGERS', Developers='AUTHORS_DEVELOPERS'}
            or {Patrons='DONORS_PATRONS', ['Platinum sponsors']='DONORS_SPONSORS_PLATINUM', ['Gold sponsors']='DONORS_SPONSORS_GOLD', ['Silver sponsors']='DONORS_SPONSORS_SILVER', ['Diamond members']='DONORS_MEMBERS_DIAMOND', ['Titanium members']='DONORS_MEMBERS_TITANIUM', ['Platinum members']='DONORS_MEMBERS_PLATINUM', ['Gold members']='DONORS_MEMBERS_GOLD'}
        local reading = false
        for line in util.lines(value) do
            if line:sub(1, 4) == "    " and reading then table.insert(text, "\t" .. util.cstring(line:trim()) .. ",\n")
            elseif line:sub(1, 3) == "## " then
                if reading then table.insert(text, "\tnullptr,\n};\n\n") end
                local section = sections[line:sub(4):trim()]
                reading = section ~= nil
                if reading then table.insert(text, "inline constexpr const char *" .. section .. "[] = {\n") end
            end
        end
        if reading then table.insert(text, "\tnullptr,\n};\n") end
    elseif name == "profiler_gen_builder" then
        local options = job.options or context.options or {}
        local function define(value) table.insert(text, "#define " .. value .. "\n") end
        if options.profiler == "tracy" then
            define("GODOT_USE_TRACY")
            if options.profiler_sample_callstack then define("TRACY_CALLSTACK 62") end
            if options.profiler_track_memory then define("GODOT_PROFILER_TRACK_MEMORY") end
            if options.profiler_record_on_demand then define("TRACY_ON_DEMAND") end
        elseif options.profiler == "perfetto" then define("GODOT_USE_PERFETTO")
        elseif options.profiler == "instruments" then
            define("GODOT_USE_INSTRUMENTS")
            if options.profiler_sample_callstack then define("INSTRUMENTS_SAMPLE_CALLSTACKS") end
        end
    elseif name == "make_default_controller_mappings" then
        local aliases = {Linux='LINUXBSD',Windows='WINDOWS',['Mac OS X']='MACOS',Android='ANDROID',iOS='APPLE_EMBEDDED',Web='WEB'}
        local order, mappings = {}, {}
        for _, source in ipairs(src) do
            local platform
            for line in util.lines(util.read(source, context)) do
                line = line:trim()
                if line:sub(1,1) == '#' then
                    local candidate = line:sub(2):trim()
                    if aliases[candidate] then
                        platform = candidate
                        if not mappings[platform] then mappings[platform] = {order={}}; table.insert(order, platform) end
                    end
                elseif platform and #line > 0 then
                    local guid = line:match("^[^,]+")
                    if not mappings[platform][guid] then table.insert(mappings[platform].order, guid) end
                    mappings[platform][guid] = line
                end
            end
        end
        table.insert(text, '#include "core/input/default_controller_mappings.h"\n#include "core/typedefs.h"\nconst char *DefaultControllerMappings::mappings[] = {\n')
        for _, platform in ipairs(order) do
            table.insert(text, '#ifdef ' .. aliases[platform] .. '_ENABLED\n')
            for _, guid in ipairs(mappings[platform].order) do table.insert(text, '\t' .. util.cstring(mappings[platform][guid]) .. ',\n') end
            table.insert(text, '#endif\n')
        end
        table.insert(text, '\tnullptr\n};\n')
    else return false end
    util.write(out, table.concat(text))
    return true
end
