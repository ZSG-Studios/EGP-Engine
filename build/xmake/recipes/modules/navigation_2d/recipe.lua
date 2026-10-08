-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_navigation_2d, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_navigation_2d = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_rvo2_2d")) then
        thirdparty_dir = "#thirdparty/rvo2/rvo2_2d/"
        thirdparty_sources = {"Agent2d.cpp", "Obstacle2d.cpp", "KdTree2d.cpp", "RVOSimulator2d.cpp"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_navigation_2d:prepend({["CPPPATH"] = {thirdparty_dir}})
        env_thirdparty = env_navigation_2d:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    end
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_navigation_2d:sources(module_obj, "*.cpp")
    env_navigation_2d:sources(module_obj, "2d/*.cpp")
    if R.truthy(env.editor_build) then
        env_navigation_2d:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
