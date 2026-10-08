-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_zip
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_zip = env_modules:clone()
    env_zip:sources(env.modules_sources, "*.cpp")
    if R.truthy(R.index(env, "tests")) then
        env_zip:sources(env.modules_sources, "./tests/*.cpp")
    end
end
