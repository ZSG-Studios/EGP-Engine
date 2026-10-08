-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_thirdparty, scene_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/misc/"
    thirdparty_sources = {"qoa.c"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_thirdparty = env:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.scene_sources = R.iadd(env.scene_sources, thirdparty_obj)
    scene_obj = {}
    env:sources(scene_obj, "*.cpp")
    env.scene_sources = R.iadd(env.scene_sources, scene_obj)
    env:depends(scene_obj, thirdparty_obj)
    if R.truthy(not R.truthy(R.index(env, "disable_2d"))) then
        graph:include("2d/recipe.lua")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_3d"))) then
        graph:include("3d/recipe.lua")
    end
    graph:include("audio/recipe.lua")
end
