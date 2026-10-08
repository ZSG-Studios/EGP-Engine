-- Shared native generator utilities. No external script runtime is used.
function read(source, context)
    if source.value ~= nil then return source.value end
    return assert(io.readfile(path.is_absolute(source.path) and source.path or path.join(context.root, source.path), {encoding = "binary"}))
end

function sourcepath(source, context)
    return path.is_absolute(source.path) and source.path or path.join(context.root, source.path)
end

function cstring(value)
    value = tostring(value)
    return '"' .. value:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\r', '\\r'):gsub('\n', '\\n'):gsub('\t', '\\t'):gsub('[%z\1-\8\11\12\14-\31\128-\255]', function(char) return string.format('\\%03o',char:byte()) end) .. '"'
end

function rawstring(value)
    local chunks = {}
    -- Small escaped literals avoid MSVC's per-literal length limit and preserve UTF-8 bytes.
    for offset = 1, #value, 2048 do table.insert(chunks, cstring(value:sub(offset, offset + 2047))) end
    if #chunks == 0 then return '""' end
    return table.concat(chunks, "\n")
end

function buffer(value)
    local lines, current = {}, {}
    for i = 1, #value do
        table.insert(current, tostring(value:byte(i)))
        if #current == 24 then table.insert(lines, table.concat(current, ", ")); current = {} end
    end
    if #current > 0 then table.insert(lines, table.concat(current, ", ")) end
    return table.concat(lines, ",\n\t")
end

function write(target, value, guard)
    local prefix = "/* THIS FILE IS GENERATED. EDITS WILL BE LOST. */\n\n"
    if guard == true or (guard == nil and (target:match("%.h$") or target:match("%.inc$"))) then prefix = prefix .. "#pragma once\n\n" end
    value = prefix .. value
    if os.isfile(target) and io.readfile(target) == value then return end
    os.mkdir(path.directory(target))
    io.writefile(target, value)
end

function sortedkeys(value)
    local keys = {}
    for key in pairs(value) do table.insert(keys, key) end
    table.sort(keys)
    return keys
end

function compress(value, context)
    assert(context.compress_executable, "Native egp_compress host tool is required")
    local input, output = os.tmpfile() .. ".input", os.tmpfile() .. ".zlib"
    io.writefile(input, value, {encoding = "binary"})
    os.vrunv(context.compress_executable, {"--input", input, "--output", output, "--format", "zlib"})
    local result = assert(io.readfile(output, {encoding = "binary"}))
    os.tryrm(input)
    os.tryrm(output)
    return result
end

function lines(value)
    value = value:gsub("\r\n", "\n")
    if value:sub(-1) ~= "\n" then value = value .. "\n" end
    return value:gmatch("([^\n]*)\n")
end
