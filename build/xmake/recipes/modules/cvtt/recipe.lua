-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_cvtt, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_cvtt = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/cvtt/"
    thirdparty_sources = {"ConvectionKernels_API.cpp", "ConvectionKernels_ETC.cpp", "ConvectionKernels_BC67.cpp", "ConvectionKernels_IndexSelector.cpp", "ConvectionKernels_BC6H_IO.cpp", "ConvectionKernels_S3TC.cpp", "ConvectionKernels_BC7_PrioData.cpp", "ConvectionKernels_SingleFile.cpp", "ConvectionKernels_BCCommon.cpp", "ConvectionKernels_Util.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_cvtt:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_thirdparty = env_cvtt:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_cvtt:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
