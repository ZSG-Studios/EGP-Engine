-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.scene_sources, "*.cpp")
    if R.truthy((function() local v = R.index(env, "disable_2d"); if not R.truthy(v) then return v end; return not R.truthy(R.index(env, "disable_advanced_gui")) end)()) then
        env:sources(env.scene_sources, "../2d/line_2d.cpp")
        env:sources(env.scene_sources, "../2d/line_builder.cpp")
        env:sources(env.scene_sources, "../2d/node_2d.cpp")
    end
end
