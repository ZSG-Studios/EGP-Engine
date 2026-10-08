-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_mobile_vr, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_mobile_vr = env_modules:clone()
    env_mobile_vr:sources(env.modules_sources, "*.cpp")
end
