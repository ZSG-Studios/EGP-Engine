-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.editor_sources, "*.cpp")
    if R.truthy(R.index(env, "rendering_device")) then
        graph:include("shader_baker/recipe.lua")
    end
end
