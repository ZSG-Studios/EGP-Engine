-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.drivers_sources, "accessibility_server_accesskit.cpp")
end
