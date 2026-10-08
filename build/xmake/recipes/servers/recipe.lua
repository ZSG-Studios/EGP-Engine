-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, lib
    env = graph:use("env")
    env.servers_sources = {}
    env:sources(env.servers_sources, "register_server_types.cpp")
    graph:include("audio/recipe.lua")
    graph:include("camera/recipe.lua")
    graph:include("debugger/recipe.lua")
    graph:include("display/recipe.lua")
    graph:include("movie_writer/recipe.lua")
    graph:include("rendering/recipe.lua")
    graph:include("text/recipe.lua")
    graph:include("navigation_2d/recipe.lua")
    graph:include("physics_2d/recipe.lua")
    graph:include("navigation_3d/recipe.lua")
    graph:include("physics_3d/recipe.lua")
    graph:include("xr/recipe.lua")
    lib = env:library("servers", env.servers_sources)
    env:prepend({["LIBS"] = {lib}})
end
