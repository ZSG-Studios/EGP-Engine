-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.servers_sources, "*.cpp")
    graph:include("effects/recipe.lua")
end
