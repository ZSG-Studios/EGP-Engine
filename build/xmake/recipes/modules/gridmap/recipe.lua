-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_gridmap, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_gridmap = env_modules:clone()
    env_gridmap:sources(env.modules_sources, "*.cpp")
    if R.truthy(env.editor_build) then
        env_gridmap:sources(env.modules_sources, "editor/*.cpp")
    end
end
