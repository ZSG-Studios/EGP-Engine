-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, force_link_header, force_link_sources, glob, lib, test_builders, tests_obj
    glob = R.glob
    test_builders = graph:builders("test_builders")
    env = graph:use("env")
    force_link_sources = {}
    for _, source in ipairs(glob.glob("**.cpp")) do
        -- Native glob paths are absolute; classify relative to the tests directory.
        local relative = (path.relative(source, graph.current):gsub("\\", "/"))
        local standalone = false
        for _, directory in ipairs({"physics/box3d/", "physics/box2d/", "compatibility_test/", "build/"}) do
            if relative:startswith(directory) then standalone = true; break end
        end
        if relative:find("/", 1, true) and not standalone then table.insert(force_link_sources, source) end
    end
    force_link_header = env:generate("force_link.gen.h", env:value(force_link_sources), env:generator(test_builders.force_link_builder))
    env:depends(force_link_header, "#build/xmake/generators/modules.lua")
    tests_obj = {}
    env:sources(tests_obj, R.add(glob.glob("*.cpp"), force_link_sources))
    lib = env:library("tests", tests_obj)
    env:prepend({["LIBS"] = {lib}})
end
