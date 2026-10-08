-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, make_virtuals
    env = graph:use("env")
    make_virtuals = graph:builders("make_virtuals")
    env:generate("gdvirtual.gen.h", "#build/xmake/generators/virtuals.lua", env:generator(make_virtuals.run))
    env:sources(env.core_sources, "*.cpp")
end
