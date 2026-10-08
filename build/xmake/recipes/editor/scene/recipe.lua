-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.editor_sources, "*.cpp")
    graph:include("2d/recipe.lua")
    graph:include("3d/recipe.lua")
    graph:include("gui/recipe.lua")
    graph:include("texture/recipe.lua")
end
