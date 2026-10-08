-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_xatlas_unwrap, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_xatlas_unwrap = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_xatlas")) then
        thirdparty_dir = "#thirdparty/xatlas/"
        thirdparty_sources = {"xatlas.cpp"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_xatlas_unwrap:prepend({["CPPPATH"] = {thirdparty_dir}})
        env_thirdparty = env_xatlas_unwrap:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_xatlas_unwrap:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
