-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.servers_sources, "*.cpp")
    graph:include("effects/recipe.lua")
    graph:include("environment/recipe.lua")
    graph:include("storage_rd/recipe.lua")
    if R.truthy(R.index(env, "forward_plus_renderer")) then
        graph:include("forward_clustered/recipe.lua")
    end
    graph:include("shaders/recipe.lua")
    graph:include("spirv-reflect/recipe.lua")
end
