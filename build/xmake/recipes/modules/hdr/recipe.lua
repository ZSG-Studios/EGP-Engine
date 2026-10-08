-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_hdr, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_hdr = env_modules:clone()
    env_hdr:sources(env.modules_sources, "*.cpp")
end
