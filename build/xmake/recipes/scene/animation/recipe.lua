-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_thirdparty, scene_obj, thirdparty_obj
    env = graph:use("env")
    thirdparty_obj = {}
    env_thirdparty = env:clone()
    env_thirdparty:disable_warnings()
    env.scene_sources = R.iadd(env.scene_sources, thirdparty_obj)
    scene_obj = {}
    env:sources(scene_obj, "*.cpp")
    env.scene_sources = R.iadd(env.scene_sources, scene_obj)
    env:depends(scene_obj, thirdparty_obj)
end
