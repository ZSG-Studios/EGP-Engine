-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.servers_sources, "*.cpp")
    graph:include("environment/recipe.lua")
    graph:include("storage/recipe.lua")
end
