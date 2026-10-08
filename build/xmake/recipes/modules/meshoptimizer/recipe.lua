-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_meshoptimizer, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_meshoptimizer = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/meshoptimizer/"
    thirdparty_sources = {"allocator.cpp", "clusterizer.cpp", "indexanalyzer.cpp", "indexcodec.cpp", "indexgenerator.cpp", "overdrawoptimizer.cpp", "partition.cpp", "quantization.cpp", "rasterizer.cpp", "simplifier.cpp", "spatialorder.cpp", "stripifier.cpp", "tangentspace.cpp", "vcacheoptimizer.cpp", "vertexcodec.cpp", "vertexfilter.cpp", "vfetchoptimizer.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_thirdparty = env_meshoptimizer:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_meshoptimizer:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
