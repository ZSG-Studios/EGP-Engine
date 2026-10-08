-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_openxr, module_obj
    env = graph:use("env")
    env_openxr = graph:use("env_openxr")
    module_obj = {}
    env_openxr:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
end
