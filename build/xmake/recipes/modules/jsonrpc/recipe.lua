-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_jsonrpc, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_jsonrpc = env_modules:clone()
    env_jsonrpc:sources(env.modules_sources, "*.cpp")
    if R.truthy(R.index(env, "tests")) then
        env_jsonrpc:sources(env.modules_sources, "./tests/*.cpp")
    end
end
