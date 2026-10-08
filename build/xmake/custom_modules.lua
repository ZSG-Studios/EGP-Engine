-- Native external-module discovery and declared option validation.
local json = import("core.base.json")

function discover(graph, configs)
    local roots, folders = {}, {}
    local requested = graph.options.custom_modules
    if requested and requested ~= "" then
        assert(type(requested) == "string", "custom_modules must contain comma-separated directories")
        for folder in requested:gmatch("[^,]+") do
            local absolute = path.absolute(folder:trim(), graph.root)
            assert(os.isdir(absolute), "Custom module directory missing: " .. absolute)
            table.insert(roots, absolute)
        end
    end
    graph.custom_names, graph.custom_folders, graph.custom_library_sources = {}, {}, {}
    for _, root in ipairs(roots) do
        local candidates = {root}
        local pattern = graph.options.custom_modules_recursive and "**/config.lua" or "*/config.lua"
        for _, file in ipairs(os.files(path.join(root, pattern))) do table.insert(candidates, path.directory(file)) end
        table.sort(candidates)
        for _, folder in ipairs(candidates) do
            if os.isfile(path.join(folder, "config.lua")) and not folders[folder] then
                folders[folder] = true
                local name = path.filename(folder)
                assert(name:match("^[a-zA-Z_][a-zA-Z0-9_]*$"), "Invalid native module name: " .. name)
                assert(not configs[name], "Duplicate custom/builtin module name: " .. name)
                assert(os.isfile(path.join(folder, "recipe.lua")), "Custom module requires a native recipe.lua: " .. folder)
                assert(os.isfile(path.join(folder, "register_types.h")), "Custom module registration header missing: " .. folder)
                local config = import("config", {rootdir = folder, anonymous = true})
                assert(type(config.can_build) == "function" and type(config.configure) == "function", "Custom module config lacks can_build/configure: " .. folder)
                configs[name] = config
                graph.custom_names[name], graph.custom_folders[name] = true, folder
                graph.environment.modules_detected[name] = folder:gsub("\\", "/")
                local flag = "module_" .. name .. "_enabled"
                if not graph.explicit[flag] then
                    if config.is_enabled then graph.options[flag] = config.is_enabled()
                    else graph.options[flag] = graph.options.modules_enabled_by_default end
                end
            end
        end
    end
    if #roots > 0 then assert(not table.empty(graph.custom_names), "No native config.lua modules found in custom_modules") end
end

function options(graph, configs)
    local descriptors, supplied = {}, {}
    if graph.options.custom_module_options and graph.options.custom_module_options ~= "" then
        assert(graph.options.custom_module_options:match("^%s*{"), "custom_module_options must be a JSON object")
        supplied = assert(json.decode(graph.options.custom_module_options), "custom_module_options must be a JSON object")
        assert(type(supplied) == "table", "custom_module_options must be a JSON object")
    end
    for name, config in pairs(configs) do
        if config.get_opts then
            for _, row in ipairs(config.get_opts(graph.options.platform) or {}) do
                local key = row.name or row[1]
                local value = row.default
                if value == nil then value = row[3] end
                assert(type(key) == "string" and value ~= nil, "Invalid native module option declaration: " .. name)
                if descriptors[key] then assert(descriptors[key].default == value, "Conflicting native module option defaults: " .. key) end
                local kind = row.type or type(value)
                descriptors[key] = {default = value, kind = kind, values = row.values}
                if not graph.explicit[key] then graph.options[key] = value end
            end
        end
    end
    for key, value in pairs(supplied) do
        assert(type(key) == "string", "custom_module_options keys must be strings")
        local descriptor = descriptors[key]
        local module = key:match("^module_(.+)_enabled$")
        assert(descriptor or (module and graph.custom_names[module]), "Undeclared custom_module_options key: " .. tostring(key))
        if descriptor then
            if descriptor.kind == "boolean" then value = import("common", {rootdir = os.scriptdir()}).enabled(value)
            else assert(type(value) == type(descriptor.default), "Invalid native module option type: " .. key) end
            if descriptor.values then assert(table.contains(descriptor.values, value), "Invalid native module option value: " .. key) end
        else value = import("common", {rootdir = os.scriptdir()}).enabled(value) end
        graph.options[key], graph.explicit[key] = value, true
    end
end

function contains(graph, directory)
    for _, root in pairs(graph.custom_folders or {}) do
        local relative = path.relative(directory, root)
        if relative == "." or (not relative:startswith("..") and not path.is_absolute(relative)) then return true end
    end
    return false
end
