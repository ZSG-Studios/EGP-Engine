-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local default_theme_builders, env
    env = graph:use("env")
    default_theme_builders = graph:builders("default_theme_builders")
    env:sources(env.scene_sources, "*.cpp")
    graph:include("icons/recipe.lua")
    env:generate("#scene/theme/default_font.gen.h", "#thirdparty/fonts/OpenSans_SemiBold.woff2", env:generator(default_theme_builders.make_fonts_header))
end
