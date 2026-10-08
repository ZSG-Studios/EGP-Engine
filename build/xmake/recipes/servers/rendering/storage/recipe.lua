-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, make_ltc_lut
    env = graph:use("env")
    make_ltc_lut = graph:builders("make_ltc_lut")
    env:sources(env.servers_sources, "*.cpp")
    env:generate("ltc_lut.gen.h", {"#build/xmake/generators/init.lua", "#servers/rendering/storage/ltc/ltc_lut1.dds", "#servers/rendering/storage/ltc/ltc_lut2.dds"}, env:generator(make_ltc_lut.run))
end
