-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    if R.truthy(not R.truthy(R.index(env, "disable_navigation_3d"))) then
        env:sources(env.servers_sources, "*.cpp")
    else
        if R.truthy(env.debug_features) then
            env:sources(env.servers_sources, "navigation_server_3d.cpp")
        end
    end
end
