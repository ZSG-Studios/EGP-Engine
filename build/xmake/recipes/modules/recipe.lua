-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local base_path, env, env_modules, glob, lib, module_tests, modules_builders, modules_enabled, name, os, path, register_module_types, test_headers
    os = R.os
    modules_builders = graph:builders("modules_builders")
    env = graph:use("env")
    env_modules = env:clone()
    env_modules:add({["CPPDEFINES"] = {"GODOT_MODULE"}})
    graph:publish("env_modules", env_modules)
    modules_enabled = env:generate("modules_enabled.gen.h", env:value(env.module_list), env:generator(modules_builders.modules_enabled_builder))
    register_module_types = env:generate("register_module_types.gen.cpp", {env:value(env.modules_detected), modules_enabled}, env:generator(modules_builders.register_module_types_builder))
    test_headers = {}
    local custom_sources = {}
    for _, __item1 in ipairs(R.iter(R.items(env.module_list))) do
        local __item2 = __item1; name = R.index(__item2, 0); path = R.index(__item2, 1)
        env.modules_sources = {}
        base_path = (function() if R.truthy(os.path.isabs(path)) then return path else return name end end)()
        graph:include(R.add(base_path, "/recipe.lua"))
        if R.truthy(env.modules_sources) then
            if graph.custom_names[name] then table.join2(custom_sources, env.modules_sources)
            else
                lib = env_modules:library(R.join({"module_", R.str(name)}, ""), env.modules_sources)
                env:prepend({["LIBS"] = {lib}})
            end
        end
        if R.truthy(R.index(env, "tests")) then
            glob = R.glob
            module_tests = R.sorted(glob.glob(os.path.join(base_path, "tests", "*.h")))
            if R.truthy(((module_tests ~= {}))) then
                test_headers = R.iadd(test_headers, module_tests)
            end
        end
    end
    table.join2(custom_sources, graph.custom_library_sources)
    if #custom_sources > 0 then env:prepend({LIBS = {env_modules:library("custom_modules", custom_sources)}}) end
    if R.truthy(R.index(env, "tests")) then
        env:generate("modules_tests.gen.h", test_headers, env:generator(modules_builders.modules_tests_builder))
    end
    env.modules_sources = {}
    env_modules:sources(env.modules_sources, register_module_types)
    lib = env_modules:library("modules", env.modules_sources)
    env:prepend({["LIBS"] = {lib}})
end
