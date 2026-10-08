-- Typed collection and emission helpers for the pinned SDK's native Lua generator.
local unpack_values = table.unpack or unpack
local json_module = import("core.base.json")
local none_value, missing_value = {}, {}

function none() return none_value end

local function collection(kind, values)
    return {_kind = kind, _values = values or {}, _keys = {}}
end

function array(values) return collection("array", values) end
function arguments(values) return array(values) end
function dict(values)
    local result = collection("dict")
    for _, pair in ipairs(values or {}) do put(result, pair[1], pair[2]) end
    return result
end
function put(value, key, item)
    if value._kind == "dict" then
        if value._values[key] == nil then table.insert(value._keys, key) end
        value._values[key] = item
        if item == nil then for index, stored in ipairs(value._keys) do if stored == key then table.remove(value._keys,index); break end end end
    else
        if key < 0 then key = #value._values + key end
        value._values[key + 1] = item
    end
end
function get(value, key)
    if type(value) == "string" then
        if key < 0 then key = #value + key end
        return value:sub(key + 1, key + 1)
    end
    if value._kind == "dict" then
        assert(value._values[key] ~= nil, "Missing SDK metadata key: " .. tostring(key))
        return value._values[key]
    end
    assert(type(key) == "number", "Wrong SDK collection kind=" .. tostring(value._kind) .. " key=" .. tostring(key))
    if key < 0 then key = #value._values + key end
    assert(value._values[key + 1] ~= nil, "SDK sequence index out of range: " .. tostring(key))
    return value._values[key + 1]
end
function len(value)
    if type(value) == "string" then return #value end
    if value._kind == "dict" then return #value._keys end
    return #value._values
end
function iter(value)
    local values = type(value) == "string" and nil or (value._kind == "dict" and value._keys or value._values)
    local index = 0
    return function()
        index = index + 1
        if values then return values[index] end
        if index <= #value then return value:sub(index, index) end
    end
end
function truth(value)
    if value == nil or value == none_value or value == false then return false end
    if type(value) == "number" then return value ~= 0 end
    if type(value) == "string" then return #value > 0 end
    if type(value) == "table" and value._kind then return len(value) > 0 end
    return true
end
function same(a, b) return a == b or (a == nil and b == none_value) or (b == nil and a == none_value) end
function logical(a, rhs, is_and)
    if (truth(a) and is_and) or (not truth(a) and not is_and) then return rhs() end
    return a
end
function choose(condition, yes, no) if truth(condition) then return yes() else return no() end end
function str(value)
    if value == nil or value == none_value then return "None" end
    if value == true then return "True" end
    if value == false then return "False" end
    if type(value) == "table" and value._path then return value._path end
    if type(value) == "table" and value._number then return value._number end
    if type(value) == "number" and value == math.floor(value) then return string.format("%.0f", value) end
    return tostring(value)
end
function add(a, b)
    if type(a) == "string" or type(b) == "string" then return str(a) .. str(b) end
    if type(a) == "table" and a._kind == "array" then
        local result = array({})
        for value in iter(a) do table.insert(result._values, value) end
        for value in iter(b) do table.insert(result._values, value) end
        return result
    end
    return a + b
end
function sub(a, b) return a - b end
function mul(a, b)
    if type(a) == "string" then return string.rep(a, b) end
    if type(b) == "string" then return string.rep(b, a) end
    return a * b
end
function div(a, b)
    if type(a) == "table" and a._path then return Path(path.join(a._path, str(b))) end
    return a / b
end
function contains(value, item)
    if type(value) == "string" then return value:find(str(item), 1, true) ~= nil end
    if value._kind == "dict" then return value._values[item] ~= nil end
    for candidate in iter(value) do if same(candidate, item) then return true end end
    return false
end
function slice(value, first, last, step)
    local count = len(value)
    first = same(first, none_value) and 0 or first
    last = same(last, none_value) and count or last
    step = same(step, none_value) and 1 or step
    if first < 0 then first = math.max(0, count + first) end
    if last < 0 then last = math.max(0, count + last) end
    first, last = math.min(count, first), math.min(count, last)
    if type(value) == "string" and step == 1 then return value:sub(first + 1, last) end
    local result = array({})
    for index = first, last - 1, step do table.insert(result._values, get(value, index)) end
    if type(value) == "string" then return table.concat(result._values) end
    return result
end
function list(value)
    local result = array({})
    if value then for item in iter(value) do table.insert(result._values, item) end end
    return result
end
function set(value)
    local result = array({})
    if value then for item in iter(value) do if not contains(result, item) then table.insert(result._values, item) end end end
    return result
end
function range(first, last, step)
    if last == nil then first, last = 0, first end
    local result = array({})
    for index = first, last - 1, step or 1 do table.insert(result._values, index) end
    return result
end
function enumerate(value)
    local result, index = array({}), 0
    for item in iter(value) do table.insert(result._values, array({index, item})); index = index + 1 end
    return result
end
function sorted(value)
    local result = list(value)
    table.sort(result._values)
    return result
end
function map(fn_value, value)
    local result = array({})
    for item in iter(value) do table.insert(result._values, call(fn_value, arguments({item}), dict({}))) end
    return result
end
function append(value, item) table.insert(value._values, item) end
function spread(value) return {_spread = value} end
function fn(names, defaults, implementation)
    return {_function = implementation, _names = names, _defaults = defaults}
end
function call(callable, positional, keywords)
    local values = {}
    for item in iter(positional) do
        if type(item) == "table" and item._spread then
            for expanded in iter(item._spread) do table.insert(values, expanded) end
        else table.insert(values, item) end
    end
    if type(callable) == "function" then return callable(unpack_values(values)) end
    assert(type(callable) == "table" and callable._function, "Invalid SDK emitter callable")
    for index, name in ipairs(callable._names) do
        if keywords and contains(keywords, name) then
            assert(values[index] == nil, "Duplicate argument: " .. name)
            values[index] = get(keywords, name)
        elseif values[index] == nil then
            assert(callable._defaults[index] ~= missing_value, "Missing argument: " .. name)
            values[index] = callable._defaults[index]
        end
    end
    return callable._function(unpack_values(values, 1, #callable._names))
end
function Path(value) return {_path = str(value)} end
function open(filename, mode)
    mode = ((mode or "r"):gsub("t", "")); if mode:sub(1,1) == "+" then mode = mode:sub(2) .. "+" end
    local file = assert(io.open(str(filename), mode))
    return {_file = file}
end
function close(value) if value and value._file then value._file:close(); value._file = nil end end
function isinstance(value, kind) return kind == dict and type(value) == "table" and value._kind == "dict" end
function unknown_type(value) return "Unknown SDK interface type: " .. str(value) end

local function replace_plain(value, needle, replacement)
    local result, cursor = {}, 1
    while true do
        local first, last = value:find(needle, cursor, true)
        if not first then table.insert(result, value:sub(cursor)); break end
        table.insert(result, value:sub(cursor, first - 1)); table.insert(result, replacement); cursor = last + 1
    end
    return table.concat(result)
end
function attr(value, name)
    if type(value) == "string" then
        if name == "replace" then return function(a, b) return replace_plain(value, a, b) end end
        if name == "lower" then return function() return value:lower() end end
        if name == "upper" then return function() return value:upper() end end
        if name == "capitalize" then return function() return value:sub(1, 1):upper() .. value:sub(2):lower() end end
        if name == "startswith" then return function(prefix) return value:sub(1, #prefix) == prefix end end
        if name == "endswith" then return function(suffix) return suffix == "" or value:sub(-#suffix) == suffix end end
        if name == "strip" then return function(chars)
            if not chars then return value:match("^%s*(.-)%s*$") end
            local first, last = 1, #value
            while first <= last and chars:find(value:sub(first, first), 1, true) do first = first + 1 end
            while last >= first and chars:find(value:sub(last, last), 1, true) do last = last - 1 end
            return value:sub(first, last)
        end end
        if name == "join" then return function(items)
            local strings = {}; for item in iter(items) do table.insert(strings, str(item)) end
            return table.concat(strings, value)
        end end
        if name == "split" or name == "splitlines" then return function(separator, maxsplit)
            local result, cursor, splits = array({}), 1, 0
            if name == "splitlines" then separator = "\n" end
            if separator == nil then for part in value:gmatch("%S+") do append(result, part) end; return result end
            while maxsplit == nil or splits < maxsplit do
                local first, last = value:find(separator, cursor, true)
                if not first then break end
                append(result, value:sub(cursor, first - 1)); cursor = last + 1; splits = splits + 1
            end
            if cursor <= #value or name ~= "splitlines" then append(result, value:sub(cursor)) end
            return result
        end end
        if name == "decode" then return function() return value end end
    elseif type(value) == "table" then
        if value._file then
            if name == "write" then return function(text) return value._file:write(text) end end
            if name == "read" then return function() return assert(value._file:read("*a")) end end
        elseif value._path then
            if name == "as_posix" then return function() return value._path:gsub("\\", "/") end end
            if name == "mkdir" then return function() os.mkdir(value._path) end end
            if name == "open" then return function(mode) return open(value._path, mode) end end
        elseif value._kind then
            if name == "append" or name == "add" then return function(item)
                if name == "append" or not contains(value, item) then append(value, item) end
            end end
            if name == "get" then return function(key, default)
                local found = value._values[key]
                if found ~= nil then return found end
                if default ~= nil then return default end
                return none_value
            end end
            if name == "keys" then return function() return list(array(value._keys)) end end
            if name == "items" then return function()
                local result = array({}); for _, key in ipairs(value._keys) do append(result, array({key, value._values[key]})) end; return result
            end end
            if name == "sort" then return function() table.sort(value._values) end end
            if name == "copy" then return function()
                if value._kind == "dict" then local result = dict({}); for _, key in ipairs(value._keys) do put(result, key, value._values[key]) end; return result end
                return list(value)
            end end
            if name == "remove" then return function(item)
                for index, candidate in ipairs(value._values) do if same(candidate, item) then table.remove(value._values, index); return end end
                raise("SDK emitter attempted to remove absent item")
            end end
        else return assert(value[name], "Unknown SDK helper: " .. name) end
    end
    raise("Unsupported SDK emitter attribute: " .. name)
end

local function parse_json(text)
    local cursor = 1
    local function whitespace() local _, last = text:find("^%s*", cursor); cursor = (last or cursor - 1) + 1 end
    local function quoted()
        local first = cursor; cursor = cursor + 1
        while true do
            local char = text:sub(cursor, cursor)
            assert(char ~= "", "Unterminated SDK JSON string")
            if char == '"' then cursor = cursor + 1; break end
            cursor = cursor + (char == "\\" and 2 or 1)
        end
        return json_module.decode(text:sub(first, cursor - 1))
    end
    local read
    read = function()
        whitespace(); local char = text:sub(cursor, cursor)
        if char == '"' then return quoted() end
        if char == "{" or char == "[" then
            local is_object, result = char == "{", char == "{" and dict({}) or array({})
            cursor = cursor + 1; whitespace()
            if text:sub(cursor, cursor) ~= (is_object and "}" or "]") then
                while true do
                    local key
                    if is_object then key = quoted(); whitespace(); assert(text:sub(cursor, cursor) == ":"); cursor = cursor + 1 end
                    local item = read()
                    if is_object then put(result, key, item) else append(result, item) end
                    whitespace(); if text:sub(cursor, cursor) ~= "," then break end
                    cursor = cursor + 1; whitespace()
                end
            end
            assert(text:sub(cursor, cursor) == (is_object and "}" or "]"), "Invalid SDK JSON container")
            cursor = cursor + 1; return result
        end
        for token, value in pairs({["true"] = true, ["false"] = false, ["null"] = none_value}) do
            if text:sub(cursor, cursor + #token - 1) == token then cursor = cursor + #token; return value end
        end
        local token = text:match("^%-?%d+%.?%d*[eE]?[%+%-]?%d*", cursor)
        assert(token, "Invalid SDK JSON value at " .. cursor)
        cursor = cursor + #token
        local number = assert(tonumber(token))
        if not token:find("[%.eE]") and math.abs(number) > 9007199254740991 then return {_number = token} end
        return number
    end
    local result = read(); whitespace(); assert(cursor > #text, "Trailing SDK JSON data"); return result
end
local function dump_json(value, indent, depth)
    depth = depth or 0
    if value == none_value then return "null" end
    if type(value) ~= "table" then
        if type(value) == "number" then return str(value) end
        return (json_module.encode(value):gsub("\\/", "/"))
    end
    if value._number then return value._number end
    local object, parts = value._kind == "dict", {}
    for index, key in ipairs(object and value._keys or value._values) do
        local item = object and value._values[key] or key
        local prefix = object and (dump_json(key, indent, depth + 1) .. ": ") or ""
        parts[index] = string.rep(" ", (depth + 1) * indent) .. prefix .. dump_json(item, indent, depth + 1)
    end
    local first, last = object and "{" or "[", object and "}" or "]"
    if #parts == 0 then return first .. last end
    return first .. "\n" .. table.concat(parts, ",\n") .. "\n" .. string.rep(" ", depth * indent) .. last
end

json = {
    load = function(file) return parse_json(attr(file, "read")()) end,
    loads = parse_json,
    dumps = fn({"value", "indent"}, {missing_value, 4}, dump_json)
}
shutil = {rmtree = function(value) os.rm(str(value)) end}
re = {sub = function(pattern, replacement, value)
    if pattern == "(.)([A-Z][a-z]+)" then return (value:gsub("(.)(%u%l+)", "%1_%2")) end
    if pattern == "([a-z0-9])([A-Z])" then return (value:gsub("([%l%d])(%u)", "%1_%2")) end
    raise("Unsupported SDK identifier pattern: " .. pattern)
end}
difflib = {unified_diff = function(first, second)
    if table.concat(first._values, "\n") == table.concat(second._values, "\n") then return array({}) end
    for index = 1, math.max(len(first), len(second)) do
        if first._values[index] ~= second._values[index] then
            return array({"@@ SDK interface line " .. index .. " @@",
                "-" .. (first._values[index] or ""), "+" .. (second._values[index] or "")})
        end
    end
    return array({})
end}
function interface_header(target, source, header)
    local module = import("build.xmake.generators.make_interface_header", {rootdir = os.projectdir()})
    return module.main(str(target), str(source), header)
end

-- Sentinel fields are values, rather than callable helpers.
_none = none_value
_missing = missing_value
function main()
    return {none = none_value, _none = none_value, _missing = missing_value,
        array=array,arguments=arguments,dict=dict,put=put,get=get,len=len,iter=iter,truth=truth,same=same,
        logical=logical,choose=choose,str=str,add=add,sub=sub,mul=mul,div=div,contains=contains,slice=slice,
        list=list,set=set,range=range,enumerate=enumerate,sorted=sorted,map=map,append=append,spread=spread,
        fn=fn,call=call,Path=Path,open=open,close=close,isinstance=isinstance,unknown_type=unknown_type,
        attr=attr,json=json,shutil=shutil,re=re,difflib=difflib,interface_header=interface_header}
end
