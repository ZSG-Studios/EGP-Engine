-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_tilemap
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_tilemap = env_modules:clone()
    env_tilemap:sources(env.modules_sources, "*.cpp")
    if R.truthy(env.editor_build) then
        env_tilemap:sources(env.modules_sources, "editor/*.cpp")
    end
end
