-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_tinyexr, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_tinyexr = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/tinyexr/"
    thirdparty_sources = {"tinyexr.cc"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_tinyexr:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_tinyexr:add({["CPPDEFINES"] = {"TINYEXR_USE_THREAD"}})
    env_tinyexr:add({["CPPDEFINES"] = {{"TINYEXR_USE_MINIZ", 0}}})
    env_thirdparty = env_tinyexr:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_tinyexr:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
