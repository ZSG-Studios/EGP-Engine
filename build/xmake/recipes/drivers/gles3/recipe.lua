-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.drivers_sources, "*.cpp")
    graph:include("shaders/recipe.lua")
    graph:include("storage/recipe.lua")
    graph:include("effects/recipe.lua")
    graph:include("environment/recipe.lua")
end
