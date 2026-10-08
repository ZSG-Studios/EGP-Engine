-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_navigation_3d, env_thirdparty, module_obj, navigation_2d_enabled, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_navigation_3d = env_modules:clone()
    thirdparty_obj = {}
    navigation_2d_enabled = (R.contains(env.module_list, "navigation_2d"))
    if R.truthy(R.index(env, "builtin_recastnavigation")) then
        thirdparty_dir = "#thirdparty/recastnavigation/Recast/"
        thirdparty_sources = {"Source/Recast.cpp", "Source/RecastAlloc.cpp", "Source/RecastArea.cpp", "Source/RecastAssert.cpp", "Source/RecastContour.cpp", "Source/RecastFilter.cpp", "Source/RecastLayers.cpp", "Source/RecastMesh.cpp", "Source/RecastMeshDetail.cpp", "Source/RecastRasterization.cpp", "Source/RecastRegion.cpp"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_navigation_3d:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "Include")}})
        env_thirdparty = env_navigation_3d:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    end
    if R.truthy(R.index(env, "builtin_rvo2_2d")) then
        thirdparty_dir = "#thirdparty/rvo2/rvo2_2d/"
        thirdparty_sources = {"Agent2d.cpp", "Obstacle2d.cpp", "KdTree2d.cpp", "RVOSimulator2d.cpp"}
        thirdparty_sources = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(thirdparty_sources)) do; local file = __item4; table.insert(__item3, R.add(thirdparty_dir, file)); end; return __item3 end)()
        env_navigation_3d:prepend({["CPPPATH"] = {thirdparty_dir}})
        if R.truthy(not R.truthy(navigation_2d_enabled)) then
            env_thirdparty = env_navigation_3d:clone()
            env_thirdparty:disable_warnings()
            env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        end
    end
    if R.truthy(R.index(env, "builtin_rvo2_3d")) then
        thirdparty_dir = "#thirdparty/rvo2/rvo2_3d/"
        thirdparty_sources = {"Agent3d.cpp", "KdTree3d.cpp", "RVOSimulator3d.cpp"}
        thirdparty_sources = (function() local __item5 = {}; for _, __item6 in ipairs(R.iter(thirdparty_sources)) do; local file = __item6; table.insert(__item5, R.add(thirdparty_dir, file)); end; return __item5 end)()
        env_navigation_3d:prepend({["CPPPATH"] = {thirdparty_dir}})
        env_thirdparty = env_navigation_3d:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    end
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_navigation_3d:sources(module_obj, "*.cpp")
    env_navigation_3d:sources(module_obj, "3d/*.cpp")
    if R.truthy(env.editor_build) then
        env_navigation_3d:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
