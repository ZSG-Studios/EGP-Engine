-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    if R.truthy(not R.truthy(R.index(env, "disable_physics_2d"))) then
        env:sources(env.servers_sources, "*.cpp")
    end
end
