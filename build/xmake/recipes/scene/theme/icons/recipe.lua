-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local default_theme_icons_builders, env, sources
    env = graph:use("env")
    default_theme_icons_builders = graph:builders("default_theme_icons_builders")
    sources = graph:files("*.svg")
    if R.truthy((R.contains({"editor", "template_debug"}, R.index(env, "target")))) then
        sources = R.iadd(sources, graph:files("debug_icons/*.svg"))
    end
    env:generate("#scene/theme/default_theme_icons.gen.h", sources, env:generator(default_theme_icons_builders.make_default_theme_icons_action))
end
