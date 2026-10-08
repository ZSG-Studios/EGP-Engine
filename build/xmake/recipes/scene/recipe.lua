-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, lib
    env = graph:use("env")
    env.scene_sources = {}
    env:sources(env.scene_sources, "*.cpp")
    graph:include("main/recipe.lua")
    graph:include("gui/recipe.lua")
    if R.truthy(not R.truthy(R.index(env, "disable_2d"))) then
        graph:include("2d/recipe.lua")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_3d"))) then
        graph:include("3d/recipe.lua")
    end
    graph:include("animation/recipe.lua")
    graph:include("audio/recipe.lua")
    graph:include("resources/recipe.lua")
    graph:include("debugger/recipe.lua")
    graph:include("theme/recipe.lua")
    lib = env:library("scene", env.scene_sources)
    env:prepend({["LIBS"] = {lib}})
end
