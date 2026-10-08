-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_vhacd, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_vhacd = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/vhacd/"
    thirdparty_sources = {"src/vhacdManifoldMesh.cpp", "src/FloatMath.cpp", "src/vhacdMesh.cpp", "src/vhacdICHull.cpp", "src/vhacdVolume.cpp", "src/VHACD-ASYNC.cpp", "src/btAlignedAllocator.cpp", "src/vhacdRaycastMesh.cpp", "src/VHACD.cpp", "src/btConvexHullComputer.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_vhacd:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "inc")}})
    env_thirdparty = env_vhacd:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_vhacd:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
