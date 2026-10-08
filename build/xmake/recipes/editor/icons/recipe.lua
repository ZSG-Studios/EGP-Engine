-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local editor_icons_builders, env, icon_sources, os, path
    env = graph:use("env")
    os = R.os
    editor_icons_builders = graph:builders("editor_icons_builders")
    icon_sources = graph:files("*.svg")
    for _, __item1 in ipairs(R.iter(env.module_icons_paths)) do
        path = __item1
        if R.truthy(not R.truthy(os.path.isabs(path))) then
            icon_sources = R.iadd(icon_sources, graph:files(R.join({"#", R.str(path), "/*.svg"}, "")))
        else
            icon_sources = R.iadd(icon_sources, graph:files(R.join({R.str(path), "/*.svg"}, "")))
        end
    end
    env:generate("#editor/themes/editor_icons.gen.h", icon_sources, env:generator(editor_icons_builders.make_editor_icons_action))
end
