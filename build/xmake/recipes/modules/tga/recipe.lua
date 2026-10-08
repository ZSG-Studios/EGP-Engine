-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_tga
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_tga = env_modules:clone()
    env_tga:sources(env.modules_sources, "*.cpp")
end
