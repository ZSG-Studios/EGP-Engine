-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, make_interface_dumper, make_interface_header, make_wrappers
    env = graph:use("env")
    make_interface_dumper = graph:builders("make_interface_dumper")
    make_interface_header = graph:builders("make_interface_header")
    make_wrappers = graph:builders("make_wrappers")
    env:generate("ext_wrappers.gen.h", "#build/xmake/generators/extension.lua", env:generator(make_wrappers.run))
    env:generate("gdextension_interface_dump.gen.h", {"gdextension_interface.json", "#build/xmake/generators/extension.lua"}, env:generator(make_interface_dumper.run))
    env:generate("gdextension_interface.gen.h", {"gdextension_interface.json", "#build/xmake/generators/make_interface_header.lua"}, env:generator(make_interface_header.run))
    env:sources(env.core_sources, "*.cpp")
end
