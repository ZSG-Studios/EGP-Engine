-- Native Lua source metadata. xmake owns compilers, archives, links and dependency scans.
local json = import("core.base.json")
local common = import("common", {rootdir = os.scriptdir()})
local methods_module = import("metadata", {rootdir = os.scriptdir()})

local env_methods = {}
local graph_methods = {}
local source_fields = {core_sources = true, servers_sources = true, scene_sources = true,
    editor_sources = true, drivers_sources = true, main_sources = true, modules_sources = true, platform_sources = true}

local function stored(value, key)
    for name, item in pairs(value) do if name == key then return item end end
end
local function duplicate(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, item in pairs(value) do result[key] = duplicate(item) end
    return result
end

local function node(graph, filename, value, literal)
    local result = {path = filename, value = value, literal = literal}
    if filename then result.abspath, result.name = filename, path.filename(filename) end
    result.read = function (self) if self.literal then return self.value end; return assert(io.readfile(self.path)) end
    result.get_contents = function (self) return assert(io.readfile(self.path, {encoding = "binary"})) end
    result.srcnode = function (self) return self end
    return table.inherit2(result, {__tostring = function (self) return self.path or tostring(self.value) end})
end

local function list(value)
    if type(value) ~= "table" or value.path or value.literal or value.library or value.environment then return {value} end
    return value
end

local function flatten_inputs(value, result)
    result = result or {}
    if type(value) == "table" and not value.path and not value.literal and not value.library and not value.environment then
        for _, item in ipairs(value) do flatten_inputs(item, result) end
    else table.insert(result, value) end
    return result
end

local function environment(graph, options)
    local result = {graph = graph, options = options}
    for _, field in ipairs({"CPPPATH", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LINKFLAGS", "LIBS", "LIBPATH", "ASFLAGS", "ARFLAGS", "RCFLAGS"}) do result[field] = {} end
    result.ENV = os.getenvs()
    result.BUILDERS = {RD_GLSL = {}, GLSL_HEADER = {}, RES = {}}
    return table.inherit2(result, {__index = function (self, key)
        if env_methods[key] then return env_methods[key] end
        if options[key] ~= nil then return options[key] end
        return false
    end})
end

function env_methods:clone()
    local result = environment(self.graph, self.options)
    for key, value in pairs(self) do
        if key == "graph" or key == "options" or source_fields[key] or key == "module_list" or key == "doc_class_path" then result[key] = value
        else result[key] = duplicate(value) end
    end
    return result
end

local function merge(self, values, prepend, unique)
    for field, value in pairs(values) do
        self[field] = self[field] or {}
        local destination = self[field]
        if type(value) == "table" and #value == 0 and not table.empty(value) then
            for key, part in pairs(value) do destination[key] = part end
        else
            local additions = {}
            local function append(part)
                if (field == "CPPPATH" or field == "LIBPATH") and (type(part) == "string" or (type(part) == "table" and part.path)) then
                    local directory = type(part) == "table" and part.path or part
                    if type(directory) == "table" then directory = directory.path end
                    assert(type(directory) == "string", "Invalid native include/library directory")
                    part = directory:sub(1, 1) == "#" and directory or path.absolute(directory, self.graph.current)
                end
                if type(part) == "table" and not part.path and not part.literal and not part.library and not part.environment and field ~= "CPPDEFINES" then
                    for _, nested in ipairs(part) do append(nested) end
                elseif not unique or not table.contains(destination, part) then table.insert(additions, part) end
            end
            for _, part in ipairs(list(value)) do append(part) end
            if prepend then for index = #additions, 1, -1 do table.insert(destination, 1, additions[index]) end
            else table.join2(destination, additions) end
        end
    end
end
function env_methods:add(values) merge(self, values, false, false) end
function env_methods:prepend(values) merge(self, values, true, false) end
function env_methods:add_unique(values) merge(self, values, false, true) end
function env_methods:prepend_unique(values) merge(self, values, true, true) end
function env_methods:get(key, default) local value = stored(self, key); if value == nil then value = self.options[key] end; return value == nil and default or value end
function env_methods:disable_warnings() self.vendor_warnings = false end
function env_methods:force_optimization_on_debug() if self.options.target ~= "template_release" then self.vendor_optimize = true end end
function env_methods:file(filename)
    if type(filename) == "table" and filename.path then return filename end
    filename = tostring(filename)
    filename = filename:gsub("%${([%w_]+)}", function (name) return tostring(self[name]) end)
    return node(self.graph, filename:sub(1, 1) == "#" and path.join(self.graph.root, filename:sub(2)) or path.absolute(filename, self.graph.current))
end
env_methods.directory = env_methods.file
function env_methods:value(value) if value == nil then value = false end; return node(self.graph, nil, value, true) end
function env_methods:files(pattern)
    local result = {}
    for _, filename in ipairs(os.files(tostring(self:file(pattern)))) do table.insert(result, self:file(filename)) end
    table.sort(result, function (a, b) return tostring(a) < tostring(b) end)
    return result
end
function env_methods:sources(destination, paths, allow_gen)
    local expanded = type(paths) == "string" and self:files(paths) or list(paths)
    for _, filename in ipairs(expanded) do
        if type(filename) == "table" and filename.environment then table.insert(destination, filename)
        else
            local input = self:file(filename)
            if type(paths) ~= "string" or not paths:find("*", 1, true) or not input.path:endswith(".gen.cpp") or allow_gen then table.insert(destination, {path = input, environment = self}) end
        end
    end
end
function env_methods:object(filename) return {path = self:file(filename), environment = self} end
function env_methods:library(name, sources, options)
    local normalized = {}
    for _, source in ipairs(flatten_inputs(sources)) do table.insert(normalized, type(source) == "table" and source.environment and source or self:object(source)) end
    if name == "#bin/godot" and self.options.library_type == "static_library" then
        local filename = self.LIBPREFIX .. "godot" .. self.PROGSUFFIX
        table.insert(self.graph.programs, {name = name, filename = filename, sources = normalized, environment = self, kind = "static"})
        return {self:file("#bin/" .. filename)}
    end
    if import("custom_modules", {rootdir = os.scriptdir()}).contains(self.graph, self.graph.current) then
        table.join2(self.graph.custom_library_sources, normalized)
        return {{library = "custom_modules"}}
    end
    local external = name:startswith("#bin/")
    local canonical = external and "external_" .. path.filename(name):gsub("^libgodot_", "") or name:gsub("#", "")
    assert(not canonical:find("[/\\]"), "Native archive target names must not contain directories")
    table.insert(self.graph.libraries, {name = canonical, sources = normalized, environment = self, kind = "static", external = external,
        filename = external and path.filename(name) .. self.PROGSUFFIX or nil})
    return {{library = canonical}}
end
function env_methods:program(name, sources, options)
    local normalized = {}
    for _, source in ipairs(flatten_inputs(sources)) do table.insert(normalized, type(source) == "table" and source.environment and source or self:object(source)) end
    local suffix = options and options.PROGSUFFIX or self.PROGSUFFIX
    local filename = self:file(type(name) == "table" and name[1] or name)
    if type(name) ~= "table" and not tostring(name):find("${PROGSUFFIX}", 1, true) and not filename.path:endswith(suffix) then filename = self:file(filename.path .. suffix) end
    table.insert(self.graph.programs, {name = name, filename = path.filename(filename.path), sources = normalized, environment = self, kind = "binary"})
    local outputs = {filename}
    if type(name) == "table" then
        outputs = {}
        for _, output in ipairs(name) do table.insert(outputs, self:file(output)) end
    end
    return outputs
end
function env_methods:shared_library(name, sources, options)
    local result = self:program(name, sources, options)
    self.graph.programs[#self.graph.programs].kind = "shared"
    if name == "#bin/godot" and table.contains({"linuxbsd", "macos"}, self.options.platform) then
        local program = self.graph.programs[#self.graph.programs]
        program.filename = "lib" .. program.filename
        result = {self:file("#bin/" .. program.filename)}
    end
    return result
end
function env_methods:resource(_, filename) return {self:object(filename)} end
function env_methods:generator(reference) return reference end
function env_methods:generate(targets, inputs, callback)
    local outputs, sources = {}, {}
    for _, output in ipairs(flatten_inputs(targets)) do table.insert(outputs, self:file(output)) end
    for _, input in ipairs(flatten_inputs(inputs)) do table.insert(sources, type(input) == "table" and input.literal and input or self:file(input)) end
    table.insert(self.graph.generators, {outputs = outputs, inputs = sources, generator = callback, environment = self})
    return outputs
end
function env_methods:depends(target, inputs)
    local function normalize(values)
        local result = {}
        for _, value in ipairs(flatten_inputs(values)) do
            if type(value) == "table" and value.environment then value = value.path end
            table.insert(result, type(value) == "string" and self:file(value) or value)
        end
        return result
    end
    table.insert(self.graph.dependencies, {normalize(target), normalize(inputs)})
end
function env_methods:after_build(program, callback) table.insert(self.graph.postbuild, {program = program, callback = callback, environment = self}) end
function env_methods:no_cache(value) return value end
function env_methods:clean() end
function env_methods:get_option() return false end
function env_methods:flatten(value) return self.graph.compat.flatten(value) end
function env_methods:add_module_version_string(name) self.module_version_string = self.module_version_string .. "." .. name end
function env_methods:module_add_dependencies(name, dependencies, optional)
    local registry = optional and self.module_optional_dependencies or self.module_dependencies
    registry[name] = registry[name] or {}
    table.join2(registry[name], dependencies)
end
function env_methods:module_check_dependencies(name)
    for _, dependency in ipairs(self.module_dependencies[name] or {}) do if not self.options["module_" .. dependency .. "_enabled"] then return false end end
    return true
end
function env_methods:detect(program) return import("lib.detect.find_program")(program) end
function env_methods:bind_method(callback, name) self[name] = function (_, ...) return callback(self, ...) end end
function env_methods:RD_GLSL(sources) return self.graph:shaders(self, "glsl_builders.build_rd_headers", sources) end
function env_methods:GLSL_HEADER(sources) return self.graph:shaders(self, "glsl_builders.build_raw_headers", sources) end

function graph_methods:use(name) return assert(self.exports[name], "Unknown metadata export: " .. name) end
function graph_methods:publish(name, value) self.exports[name] = value end
function graph_methods:include(recipe)
    local original = path.absolute(recipe, self.current)
    local relative = path.relative(original, self.root)
    local module = relative:gsub("\\", "/"):gsub("%.lua$", ""):gsub("/", ".")
    local custom = import("custom_modules", {rootdir = os.scriptdir()})
    local runner
    if custom.contains(self, path.directory(original)) then
        runner = import(path.basename(original), {rootdir = path.directory(original), anonymous = true})
    else runner = import(module, {rootdir = path.join(self.root, "build/xmake/recipes"), anonymous = true}) end
    local previous = self.current
    self.current = path.directory(original)
    local result = runner.main(self)
    self.current = previous
    return result
end
function graph_methods:builders(module)
    if module == "platform_methods" then return methods_module.bind(self) end
    local prefix = import("builder_names", {rootdir = os.scriptdir()}).get(module) or module
    return table.inherit2({}, {__index = function (_, name) return prefix .. "." .. name end})
end
function graph_methods:methods() return methods_module.bind(self) end
function graph_methods:mono_configure() return {configure = methods_module.bind(self).mono_configure} end
function graph_methods:original_file() return path.join(self.current, "recipe.lua") end
function graph_methods:file(filename) return self.environment:file(filename) end
function graph_methods:directory(filename) return self:file(filename) end
function graph_methods:files(pattern) return self.environment:files(pattern) end
function graph_methods:value(value) return self.environment:value(value) end
function graph_methods:get_option() return false end
function graph_methods:clean() end
function graph_methods:copy() return "copy" end
function graph_methods:message(level, message)
    local handler = assert(methods_module.bind(self)[level], "Invalid metadata message severity")
    return handler(message)
end
function graph_methods:abort(code) raise("Unsupported engine build configuration: %s", code) end
function graph_methods:shaders(env, builder, inputs)
    local result = {}
    for _, filename in ipairs(list(inputs)) do
        local source = env:file(filename)
        table.join2(result, env:generate(source.path .. ".gen.h", {source}, builder))
    end
    return result
end

function graph_methods:configure()
    local env = self.environment
    common.configure(env, self.options, self.explicit)
    local configs = {}
    for _, directory in ipairs(os.dirs(path.join(self.root, "modules/*"))) do
        local name = path.filename(directory)
        local filename = path.join(self.root, "build/xmake/recipes/modules", name, "config.lua")
        if os.isfile(filename) then
            local config = import("modules." .. name .. ".config", {rootdir = path.join(self.root, "build/xmake/recipes"), anonymous = true})
            configs[name] = config
            local flag = "module_" .. name .. "_enabled"
            if not self.explicit[flag] then
                if config.is_enabled then self.options[flag] = config.is_enabled(self)
                else self.options[flag] = self.options.modules_enabled_by_default end
            end
            env.modules_detected[name] = "modules/" .. name
        end
    end
    local custom = import("custom_modules", {rootdir = os.scriptdir()})
    custom.discover(self, configs)
    custom.options(self, configs)
    for _, name in ipairs(table.keys(configs)) do
        local config = configs[name]
        env.current_module = name
        if self.options["module_" .. name .. "_enabled"] and config.can_build(env, self.options.platform) then
            config.configure(env)
            env.module_list[name] = self.custom_folders[name] or "modules/" .. name
        else self.options["module_" .. name .. "_enabled"] = false end
    end
    local changed = true
    while changed do
        changed = false
        for name in pairs(env.module_list) do
            if not env:module_check_dependencies(name) then env.module_list[name], self.options["module_" .. name .. "_enabled"], changed = nil, false, true end
        end
    end
    self.module_order = {}
    local visited, visiting = {}, {}
    local function visit(name)
        if visited[name] or not env.module_list[name] then return end
        assert(not visiting[name], "Circular module dependency: " .. name)
        visiting[name] = true
        for _, dependency in ipairs(env.module_dependencies[name] or {}) do visit(dependency) end
        for _, dependency in ipairs(env.module_optional_dependencies[name] or {}) do visit(dependency) end
        visiting[name], visited[name] = nil, true
        table.insert(self.module_order, name)
    end
    local names = table.keys(env.module_list); table.sort(names)
    for _, name in ipairs(names) do visit(name) end
    table.inherit2(env.module_list, {__index = function (_, key) if key == "__order" then return self.module_order end end})
    for _, name in ipairs(self.module_order) do
        local config = configs[name]
        if config.get_doc_classes then
            local directory = config.get_doc_path and config.get_doc_path() or "doc_classes"
            for _, class in ipairs(config.get_doc_classes()) do env.doc_class_path[class] = self.custom_folders[name] and path.join(self.custom_folders[name], directory):gsub("\\", "/") or "modules/" .. name .. "/" .. directory end
        end
        table.insert(env.module_icons_paths, self.custom_folders[name] and path.join(self.custom_folders[name], "icons") or "modules/" .. name .. "/icons")
    end
    for _, filename in ipairs(os.files(path.join(self.root, "platform/*/doc_classes/*.xml"))) do env.doc_class_path[path.basename(filename)] = path.relative(path.directory(filename), self.root) end
    common.suffix(env, self.options)
    import("platform_packages", {rootdir = os.scriptdir()}).configure(self)
    for _, section in ipairs({"core", "servers", "scene", "editor", "drivers", "platform", "modules", "main"}) do
        if section ~= "editor" or env.editor_build then self:include(section .. "/recipe.lua") end
    end
    if self.options.tests then self:include("tests/recipe.lua") end
    self:include("platform/" .. self.options.platform .. "/recipe.lua")
end

function graph_methods:serialize()
    local function relative(filename) return (path.relative(filename, self.root):gsub("\\", "/")) end
    local function policy(env)
        local result = {}
        for _, key in ipairs({"CPPPATH", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "ASFLAGS", "ARFLAGS", "RCFLAGS", "LINKFLAGS", "LIBPATH", "vendor_warnings", "vendor_optimize", "swift"}) do result[key] = stored(env, key) end
        result.LIBS = import("linking", {rootdir = os.scriptdir()}).references(stored(env, "LIBS"), self.root)
        result.LIBS_EXTERNAL = import("linking", {rootdir = os.scriptdir()}).references(stored(env, "LIBS_EXTERNAL"), self.root)
        local closure_args = env.ENV and env.ENV.EMCC_CLOSURE_ARGS
        if closure_args ~= nil then
            assert(type(closure_args) == "string", "Closure compiler arguments must be a string")
            result.BUILD_ENV = {EMCC_CLOSURE_ARGS = closure_args}
        end
        return result
    end
    local function sources(list)
        local result = {}
        for _, item in ipairs(list) do table.insert(result, {path = relative(item.path.path), policy = policy(item.environment)}) end
        return result
    end
    local libraries, programs, generators = {}, {}, {}
    for _, library in ipairs(self.libraries) do table.insert(libraries, {name = library.name, sources = sources(library.sources), policy = policy(library.environment), kind = library.kind, external = library.external, filename = library.filename}) end
    for _, program in ipairs(self.programs) do table.insert(programs, {name = program.name, filename = program.filename, sources = sources(program.sources), policy = policy(program.environment), kind = program.kind}) end
    for _, generator in ipairs(self.generators) do
        local outputs, inputs = {}, {}
        for _, output in ipairs(generator.outputs) do table.insert(outputs, relative(output.path)) end
        for _, input in ipairs(generator.inputs) do table.insert(inputs, input.literal and {value = input.value, literal = true} or {path = relative(input.path)}) end
        table.insert(generators, {outputs = outputs, inputs = inputs, generator = generator.generator})
    end
    local dependencies = {}
    local function references(values)
        local result = {}
        for _, value in ipairs(values) do
            if value.library then table.insert(result, {kind = "target", name = value.library})
            elseif value.path then table.insert(result, {kind = "file", path = relative(value.path)})
            elseif value.literal then table.insert(result, {kind = "value", value = value.value})
            else raise("Unsupported explicit dependency reference") end
        end
        return result
    end
    for _, dependency in ipairs(self.dependencies) do
        table.insert(dependencies, {targets = references(dependency[1]), inputs = references(dependency[2])})
    end
    return {libraries = libraries, programs = programs, generators = generators, dependencies = dependencies, global_policy = policy(self.environment), options = self.options, modules = self.module_order}
end

function new(root, options)
    local explicit = {}; for key in pairs(options) do explicit[key] = true end
    local defaults = json.loadfile(path.join(root, "build/xmake/defaults.json"))
    for key, value in pairs(defaults) do if options[key] == nil then options[key] = value end end
    local graph = {root = root, current = root, options = options, explicit = explicit, libraries = {}, programs = {}, generators = {}, dependencies = {}, postbuild = {}, exports = {}}
    table.inherit2(graph, graph_methods)
    graph.compat = import("recipe_compat", {rootdir = os.scriptdir()}).new(graph)
    graph.environment = environment(graph, options)
    graph.exports.env = graph.environment
    return graph
end
