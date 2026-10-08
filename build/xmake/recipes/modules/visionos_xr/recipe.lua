-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_visionos_xr
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_visionos_xr = env_modules:clone()
    env_visionos_xr:sources(env.modules_sources, "*.mm")
end
