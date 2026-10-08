-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_noise, module_obj, thirdparty_dir
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_noise = env_modules:clone()
    thirdparty_dir = "#thirdparty/noise/"
    env_noise:prepend({["CPPPATH"] = {thirdparty_dir}})
    module_obj = {}
    env_noise:sources(module_obj, "*.cpp")
    if R.truthy(env.editor_build) then
        env_noise:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
end
