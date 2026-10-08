-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, force_link_header, force_link_sources, glob, lib, test_builders, tests_obj
    glob = R.glob
    test_builders = graph:builders("test_builders")
    env = graph:use("env")
    force_link_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(glob.glob("*/**/*.cpp", {["recursive"] = true}))) do; local source = __item2; if R.truthy(not R.truthy(R.startswith(R.replace(source, "\\", "/"), "physics/box3d/"))) then; table.insert(__item1, source); end; end; return __item1 end)()
    force_link_header = env:generate("force_link.gen.h", env:value(force_link_sources), env:generator(test_builders.force_link_builder))
    env:depends(force_link_header, "#build/xmake/generators/modules.lua")
    tests_obj = {}
    env:sources(tests_obj, R.add(glob.glob("*.cpp"), force_link_sources))
    lib = env:library("tests", tests_obj)
    env:prepend({["LIBS"] = {lib}})
end
