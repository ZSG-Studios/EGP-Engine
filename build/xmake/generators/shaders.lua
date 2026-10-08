local util = import("util", {rootdir = os.scriptdir()})
local rd_templates = import("glsl_templates", {rootdir = os.scriptdir()})

local stages = {"vertex", "fragment", "compute", "raygen", "any_hit", "closest_hit", "miss", "intersection"}

local function raw(value)
    if type(value) == "table" then value = table.concat(value, "\n") .. "\n" end
    local segments, offset = {}, 1
    while offset <= #value + 1 do
        local segment = value:sub(offset, offset + 16383)
        offset = offset + 16384
        if #segment == 16384 then
            local last, start = nil, 1
            while true do
                local found = segment:find("\n\n", start, true)
                if not found then break end
                last, start = found, found + 1
            end
            if last then
                segment = segment:sub(1, last)
                offset = offset - 16384 + last
            elseif segment:byte(-1) >= 128 then
                local first = #segment
                while first > 0 and segment:byte(first) >= 128 and segment:byte(first) < 192 do first = first - 1 end
                local lead = segment:byte(first)
                local length = lead < 224 and 2 or (lead < 240 and 3 or 4)
                if length > #segment - first + 1 then
                    offset = offset - (#segment - first + 1)
                    segment = segment:sub(1, first - 1)
                end
            end
        end
        assert(not segment:find(")<!>\"", 1, true), "Shader contains the reserved raw literal delimiter")
        table.insert(segments, 'R"<!>(' .. segment .. ')<!>"')
    end
    return #segments == 1 and segments[1] or "(" .. table.concat(segments, " ") .. ")"
end

local function classname(filename, suffix)
    local value = path.filename(filename):gsub("%.glsl$", "")
    local result, previous = {}, false
    for index = 1, #value do
        local char = value:sub(index, index)
        if char:match("%a") then
            table.insert(result, previous and char:lower() or char:upper())
            previous = true
        else table.insert(result, char); previous = false end
    end
    return table.concat(result):gsub("_", ""):gsub("%.", "") .. suffix
end

local function lines(filename)
    local text = assert(io.readfile(filename)):gsub("\r", "")
    local result, position = {}, 1
    while position <= #text do
        local ending = text:find("\n", position, true)
        table.insert(result, text:sub(position, ending or #text))
        position = ending and ending + 1 or #text + 1
    end
    return result
end

local function data()
    local result = {reading = "", uniforms = {}, variant_names = {}, variant_defines = {},
        specialization_names = {}, specialization_values = {}, texunits = {}, ubos = {}, feedbacks = {}}
    for _, stage in ipairs(stages) do result[stage], result[stage .. "_included"] = {}, {} end
    return result
end

local function uniform_names(value)
    value = value:gsub("uniform", ""):gsub("highp", ""):gsub(";", ""):gsub("{", "")
    local result = {}
    for part in (value .. ","):gmatch("(.-),") do
        local name = part:trim():match("([^%s]+)$") or ""
        name = name:gsub("%[.*$", "")
        table.insert(result, name)
    end
    return result
end

local function append_unique(values, item)
    if not table.contains(values, item) then table.insert(values, item) end
end

local function parse(filename, header, context, gles, depth)
    assert(depth < 128, "Shader include depth exceeded: " .. filename)
    local source = lines(filename)
    local index = 1
    while index <= #source do
        local line = source[index]
        if not gles then
            local comment = line:find("//", 1, true)
            if comment then line = line:sub(1, comment - 1) end
        end
        local stage = line:match("#%[([%w_]+)%]")
        if stage and (table.contains(stages, stage) or gles and (stage == "modes" or stage == "specializations")) then
            if stage ~= "modes" then header.reading = stage end
            index = index + 1
        elseif gles and line:find("=", 1, true) and (header.reading == "" or header.reading == "specializations") then
            local name, value = line:match("^(.-)=(.*)$")
            if header.reading == "" then
                table.insert(header.variant_names, name:trim():upper()); table.insert(header.variant_defines, value:trim())
            else table.insert(header.specialization_names, name:trim()); table.insert(header.specialization_values, value) end
            index = index + 1
        else
            while line and line:find("#include ", 1, true) do
                local include = line:gsub("#include ", ""):trim():sub(2, -2)
                local included = include:startswith("thirdparty/") and path.join(context.root, include) or path.absolute(include, path.directory(filename))
                local registry = header[header.reading .. "_included"]
                if registry and not registry[included] then registry[included] = true; parse(included, header, context, gles, depth + 1) end
                index = index + 1
                line = source[index]
            end
            if line then
                if gles then
                    local lower, comment = line:lower(), line:find("//", 1, true)
                    local plain = comment and line:sub(1, comment - 1) or line
                    if line:find("uniform", 1, true) and lower:find("texunit:", 1, true) then
                        local unit = line:sub(assert(line:find(":", 1, true)) + 1):trim()
                        unit = unit == "auto" and -1 or assert(tonumber(unit), "Invalid shader texture unit")
                        for _, name in ipairs(uniform_names(plain)) do
                            local exists = false; for _, pair in ipairs(header.texunits) do if pair[1] == name then exists = true end end
                            if not exists then table.insert(header.texunits, {name, unit}) end
                        end
                    elseif line:find("uniform", 1, true) and lower:find("ubo:", 1, true) then
                        local unit = assert(tonumber(line:sub(assert(line:find(":", 1, true)) + 1):trim()), "Invalid shader UBO unit")
                        for _, name in ipairs(uniform_names(plain)) do
                            local exists = false; for _, pair in ipairs(header.ubos) do if pair[1] == name then exists = true end end
                            if not exists then table.insert(header.ubos, {name, unit}) end
                        end
                    elseif line:find("uniform", 1, true) and not line:find("{", 1, true) and line:find(";", 1, true) then
                        for _, name in ipairs(uniform_names(line)) do append_unique(header.uniforms, name) end
                    end
                    if (line:trim():startswith("out ") or line:trim():startswith("flat ")) and line:find("tfb:", 1, true) then
                        local clean = line:gsub("flat ", ""):gsub("out ", ""):gsub("highp ", ""):gsub(";", "")
                        local name, binding = clean:match("^[^%s]+%s+(.-)//(.*)$")
                        if binding then table.insert(header.feedbacks, {name:trim(), binding:gsub("tfb:", ""):trim()}) end
                    end
                end
                if header[header.reading] then table.insert(header[header.reading], (line:gsub("\r", ""):gsub("\n", ""))) end
                index = index + 1
            elseif header[header.reading] then
                table.insert(header[header.reading], "")
            end
        end
    end
end

local function template(module, name, values)
    return (module.get(name):gsub("@(.-)@", function (key) return tostring(assert(values[key], "Missing shader template variable: " .. key)) end))
end

local function emit_rd(target, source, context)
    local header = data()
    parse(source, header, context, false, 0)
    local values = {class_name = classname(source, "ShaderRD"), vertex_code = raw(header.vertex), fragment_code = raw(header.fragment), compute_code = raw(header.compute)}
    local body = {template(rd_templates, "197", values)}
    local raytracing = false
    for _, stage in ipairs({"raygen", "any_hit", "closest_hit", "miss", "intersection"}) do if #header[stage] > 0 then raytracing = true end end
    if raytracing then
        for _, stage in ipairs({"raygen", "any_hit", "closest_hit", "miss", "intersection"}) do
            table.insert(body, #header[stage] > 0 and "\t\tstatic const char _" .. stage .. "_code[] = {\n" .. raw(header[stage]) .. "\n\t\t};\n" or "\t\tstatic const char *_" .. stage .. "_code = nullptr;\n")
        end
        table.insert(body, template(rd_templates, "217", values))
    else table.insert(body, template(rd_templates, #header.compute > 0 and "221" or "230", values)) end
    table.insert(body, template(rd_templates, "241", values))
    util.write(target, table.concat(body))
end


local function raw_shader(filename, depth)
    assert(depth < 128, "Raw shader include depth exceeded")
    local output = {}
    for _, line in ipairs(lines(filename)) do
        if line:find("#include ", 1, true) then
            local included = line:gsub("#include ", ""):trim():sub(2, -2)
            table.insert(output, raw_shader(path.absolute(included, path.directory(filename)), depth + 1))
        else table.insert(output, line) end
    end
    return table.concat(output)
end

function generate(job, context)
    local name = job.builder:match("([%w_]+)$")
    if name ~= "build_rd_headers" and name ~= "build_raw_headers" then return false end
    for index, target in ipairs(job.targets) do
        local source = job.sources[index].path
        source = path.is_absolute(source) and source or path.join(context.root, source)
        if name == "build_rd_headers" then emit_rd(target, source, context)
        else
            local variable = path.filename(source):gsub("%.glsl", "_shader_glsl")
            util.write(target, "static const char " .. variable .. "[] = {\n" .. raw(raw_shader(source, 0)) .. "\n};\n")
        end
    end
    return true
end
