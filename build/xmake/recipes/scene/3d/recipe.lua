-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.scene_sources, "*.cpp")
    if R.truthy(not R.truthy(R.index(env, "disable_physics_3d"))) then
        graph:include("physics/recipe.lua")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_navigation_3d"))) then
        graph:include("navigation/recipe.lua")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_xr"))) then
        graph:include("xr/recipe.lua")
    end
end
