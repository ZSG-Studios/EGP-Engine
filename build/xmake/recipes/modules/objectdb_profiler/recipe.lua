-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_objdb, module_obj
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_objdb = env_modules:clone()
    module_obj = {}
    if R.truthy(env_objdb.debug_features) then
        env_objdb:sources(module_obj, "*.cpp")
        if R.truthy(env.editor_build) then
            env_objdb:sources(module_obj, "editor/*.cpp")
            env_objdb:sources(module_obj, "editor/data_viewers/*.cpp")
        end
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
end
