-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_gdscript, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_gdscript = env_modules:clone()
    env_gdscript:sources(env.modules_sources, "*.cpp")
    if R.truthy(env.editor_build) then
        env_gdscript:sources(env.modules_sources, "./editor/*.cpp")
        graph:include("editor/script_templates/recipe.lua")
        if R.truthy((function() local v = R.index(env, "module_jsonrpc_enabled"); if not R.truthy(v) then return v end; return R.index(env, "module_websocket_enabled") end)()) then
            env_gdscript:sources(env.modules_sources, "./language_server/*.cpp")
        else
            env_gdscript:add({["CPPDEFINES"] = {"GDSCRIPT_NO_LSP"}})
            env:add({["CPPDEFINES"] = {"GDSCRIPT_NO_LSP"}})
        end
    end
    if R.truthy(R.index(env, "tests")) then
        env_gdscript:add({["CPPDEFINES"] = {"TESTS_ENABLED"}})
        env_gdscript:sources(env.modules_sources, "./tests/*.cpp")
    end
end
