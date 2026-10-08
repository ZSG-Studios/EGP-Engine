-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.drivers_sources, "*.cpp")
    env:add({["CPPDEFINES"] = {"XAUDIO2_ENABLED"}})
    env:add({["LINKFLAGS"] = {"xaudio2_8.lib"}})
end
