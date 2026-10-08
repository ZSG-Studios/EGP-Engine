-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.editor_sources, "*.cpp")
    env:sources(env.editor_sources, "3d/*.cpp")
end
