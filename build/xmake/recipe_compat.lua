-- Bounded native Lua data operations for build recipe metadata.
-- This module does not execute Python, compile, link, or schedule builds.
local json = import("core.base.json")

function dict(value)
    return table.inherit2(value, {__index = function (_, key) if key == "__recipe_dictionary" then return true end end})
end

function truthy(value)
    if value == nil or value == false then return false end
    if type(value) == "number" then return value ~= 0 end
    if type(value) == "string" then return #value > 0 end
    if type(value) == "table" then
        for key in pairs(value) do if key ~= "__order" then return true end end
        return false
    end
    return true
end

function str(value)
    if value == nil then return "None" end
    if value == true then return "True" end
    if value == false then return "False" end
    return tostring(value)
end

function keys(value)
    local result = {}
    if value.__order then
        for _, key in ipairs(value.__order) do if value[key] ~= nil then table.insert(result, key) end end
        return result
    end
    for key in pairs(value) do if key ~= "__order" then table.insert(result, key) end end
    table.sort(result, function (a, b) return tostring(a) < tostring(b) end)
    return result
end

function iter(value)
    assert(type(value) == "table", "Expected native recipe list/table, got " .. type(value))
    if not value.__recipe_dictionary and (#value > 0 or table.empty(value)) then return value end
    return keys(value)
end

function list(value)
    local result = {}
    for _, item in ipairs(iter(value)) do table.insert(result, item) end
    return result
end

function items(value)
    local result = {}
    for _, key in ipairs(keys(value)) do table.insert(result, {key, value[key]}) end
    return result
end

function values(value)
    local result = {}
    for _, key in ipairs(keys(value)) do table.insert(result, value[key]) end
    return result
end

function index(value, key)
    if type(key) == "number" and not (type(value) == "table" and value.__recipe_dictionary) then
        local position = key < 0 and #value + key + 1 or key + 1
        if type(value) == "string" then return value:sub(position, position) end
        assert(position >= 1 and position <= #value, "Recipe list index out of range: " .. key)
        return value[position]
    end
    return value[key]
end

function setindex(value, key, item)
    if type(key) == "number" and not value.__recipe_dictionary then key = key < 0 and #value + key + 1 or key + 1 end
    value[key] = item
end

function add(a, b)
    if type(a) == "table" and type(b) == "table" then
        local result = list(a)
        for _, value in ipairs(b) do table.insert(result, value) end
        return result
    end
    if type(a) == "number" and type(b) == "number" then return a + b end
    assert(type(a) == "string" and type(b) == "string", "Invalid recipe concatenation")
    return a .. b
end

function iadd(a, b)
    if type(a) ~= "table" then return add(a, b) end
    for _, value in ipairs(b) do table.insert(a, value) end
    return a
end

function append(target, value) table.insert(target, value) end
function extend(target, source) for _, value in ipairs(iter(source)) do table.insert(target, value) end end
function insert(target, position, value)
    position = position < 0 and math.max(1, #target + position + 1) or math.min(#target + 1, position + 1)
    table.insert(target, position, value)
end
function remove(target, value)
    for position, item in ipairs(target) do if item == value then table.remove(target, position); return end end
    raise("Recipe list.remove value not found: " .. str(value))
end
function get(target, key, default) if target[key] == nil then return default end; return target[key] end
function contains(target, value)
    if type(target) == "string" then return target:find(value, 1, true) ~= nil end
    if target.__recipe_dictionary or (#target == 0 and not table.empty(target)) then return target[value] ~= nil end
    for _, item in ipairs(target) do if item == value then return true end end
    return false
end
function startswith(value, prefixes)
    if type(prefixes) == "table" then for _, prefix in ipairs(prefixes) do if startswith(value, prefix) then return true end end; return false end
    return value:sub(1, #prefixes) == prefixes
end
function endswith(value, suffixes)
    if type(suffixes) == "table" then for _, suffix in ipairs(suffixes) do if endswith(value, suffix) then return true end end; return false end
    return #suffixes == 0 or value:sub(-#suffixes) == suffixes
end
function replace(value, before, after)
    assert(#before > 0, "Empty replacement patterns are unsupported in recipe metadata")
    local parts, position = {}, 1
    while true do
        local first, last = value:find(before, position, true)
        if not first then table.insert(parts, value:sub(position)); break end
        table.insert(parts, value:sub(position, first - 1)); table.insert(parts, after); position = last + 1
    end
    return table.concat(parts)
end
function strip(value) return value:match("^%s*(.-)%s*$") end
function split(value, separator)
    local result = {}
    if separator == nil then for part in value:gmatch("%S+") do table.insert(result, part) end; return result end
    assert(#separator > 0, "Empty recipe split separator")
    local start = 1
    while true do
        local first, last = value:find(separator, start, true)
        if not first then table.insert(result, value:sub(start)); break end
        table.insert(result, value:sub(start, first - 1)); start = last + 1
    end
    return result
end
function join(a, b)
    if type(a) == "string" then return table.concat(b, a) end
    return table.concat(a, b)
end
function format(template, ...)
    local values, index = {...}, 0
    return template:gsub("{}", function () index = index + 1; return str(values[index]) end)
end
function int(value)
    local number = tonumber(value)
    assert(number and number == number and number ~= math.huge and number ~= -math.huge, "Invalid integer: " .. str(value))
    return number < 0 and math.ceil(number) or math.floor(number)
end
function len(value) if type(value) == "table" and value.__recipe_dictionary then return #keys(value) end; return #value end
function enumerate(value)
    local result = {}
    for position, item in ipairs(iter(value)) do table.insert(result, {position - 1, item}) end
    return result
end
function map(callback, value)
    local result = {}
    for _, item in ipairs(iter(value)) do table.insert(result, callback(item)) end
    return result
end
function sort(value, options)
    local key = options and options.key
    table.sort(value, function (a, b) return key and key(a) < key(b) or (not key and str(a) < str(b)) end)
end
function sorted(value, options) local result = list(value); sort(result, options); return result end
function set(value)
    local result, seen = {}, {}
    for _, item in ipairs(iter(value)) do if not seen[item] then table.insert(result, item); seen[item] = true end end
    return result
end
function isinstance(value, classes)
    if type(classes) == "table" then for _, class in ipairs(classes) do if isinstance(value, class) then return true end end; return false end
    if classes == "str" or classes == "bytes" then return type(value) == "string" end
    if classes == "list" or classes == "tuple" then return type(value) == "table" and not value.path and not value.library end
    raise("Unsupported recipe type check: " .. classes)
end
function repr(value)
    if type(value) == "string" then return "'" .. value:gsub("\\", "\\\\"):gsub("'", "\\'") .. "'" end
    if type(value) == "table" then local parts = {}; for _, item in ipairs(value) do table.insert(parts, repr(item)) end; return "[" .. table.concat(parts, ", ") .. "]" end
    return str(value)
end
function flatten(value)
    if type(value) ~= "table" or value.path or value.literal or value.library or value.environment then return {value} end
    local result = {}; for _, item in ipairs(value) do extend(result, flatten(item)) end; return result
end

function BoolVariable(name, description, default)
    return {name = name, description = description, default = default, type = "boolean"}
end

function new(graph)
    graph = graph or {root = os.projectdir(), current = os.projectdir()}
    local result = {dict = dict, truthy = truthy, str = str, keys = keys, iter = iter, list = list, items = items, values = values,
        index = index, setindex = setindex, add = add, iadd = iadd, append = append, extend = extend, insert = insert,
        remove = remove, get = get, contains = contains, startswith = startswith, endswith = endswith, replace = replace,
        strip = strip, split = split, join = join, format = format, int = int, len = len, enumerate = enumerate,
        map = map, sort = sort, sorted = sorted, set = set, isinstance = isinstance, repr = repr, flatten = flatten, BoolVariable = BoolVariable}
    local function resolve(value)
        local filename = tostring(value)
        if filename:sub(1, 1) == "#" then return path.join(graph.root, filename:sub(2)) end
        return path.absolute(filename, graph.current)
    end
    local path_methods = {}
    function path_methods:absolute() return result.Path(resolve(self.value)) end
    function path_methods:is_dir() return os.isdir(resolve(self.value)) end
    function path_methods:is_file() return os.isfile(resolve(self.value)) end
    function path_methods:read_text() return assert(io.readfile(resolve(self.value))) end
    result.Path = function (value)
        local filename = tostring(value)
        return table.inherit2({value = filename}, {__index = function (self, key)
            if key == "stem" then return path.basename(self.value) end
            return path_methods[key]
        end, __tostring = function (self) return self.value end})
    end
    result.div = function (a, b)
        if type(a) == "number" and type(b) == "number" then return a / b end
        return result.Path(path.join(tostring(a), tostring(b)))
    end
    local function glob(pattern)
        local files = os.files(resolve(pattern)); table.sort(files); return files
    end
    result.glob = table.inherit2({glob = glob}, {__call = function (_, pattern) return glob(pattern) end})
    result.pathlib = {Path = result.Path}
    result.json = {loads = function (text) return assert(json.decode(text)) end}
    local function walk(directory)
        local base = resolve(directory)
        if not os.isdir(base) then return {} end
        local directories = os.dirs(path.join(base, "**"))
        table.insert(directories, 1, base)
        table.sort(directories)
        local rows = {}
        for _, folder in ipairs(directories) do
            local children, files = {}, {}
            for _, child in ipairs(os.dirs(path.join(folder, "*"))) do table.insert(children, path.filename(child)) end
            for _, file in ipairs(os.files(path.join(folder, "*"))) do table.insert(files, path.filename(file)) end
            table.sort(children); table.sort(files)
            table.insert(rows, {folder, children, files})
        end
        return rows
    end
    result.os = {walk = walk, environ = os.getenvs(), getenv = os.getenv, name = os.host() == "windows" and "nt" or "posix", getcwd = function () return graph.current end,
        path = {exists = function (value) return os.isfile(resolve(value)) or os.isdir(resolve(value)) end,
            isabs = path.is_absolute, abspath = resolve, join = path.join,
            splitext = function (value) return {path.join(path.directory(value), path.basename(value)), path.extension(value)} end}}
    result.sys = {platform = ({windows = "win32", macosx = "darwin", linux = "linux"})[os.host()] or os.host(),
        exit = function (code) raise("Recipe requested exit: " .. code) end}
    result.platform = {machine = function () return ({x64 = "x86_64", x86 = "i386"})[os.arch()] or os.arch() end}
    result.re = {match = function (pattern, text)
        assert(pattern == "([0-9]*).([0-9]*).([0-9]*)-?([0-9.+]*)", "Unsupported metadata regular expression")
        local all, major, minor, patch, suffix = text:match("^((%d*).(%d*).(%d*)%-?([%d%.%+]*))")
        if not all then return nil end
        local groups = {all, major, minor, patch, suffix}
        return {group = function (_, position) return index(groups, position) end}
    end}
    return result
end
