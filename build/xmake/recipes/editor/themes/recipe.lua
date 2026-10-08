-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local editor_theme_builders, env, flist
    env = graph:use("env")
    editor_theme_builders = graph:builders("editor_theme_builders")
    flist = graph:files("#thirdparty/fonts/*.ttf")
    R.extend(flist, graph:files("#thirdparty/fonts/*.otf"))
    R.extend(flist, graph:files("#thirdparty/fonts/*.woff"))
    R.extend(flist, graph:files("#thirdparty/fonts/*.woff2"))
    R.sort(flist)
    env:generate("#editor/themes/builtin_fonts.gen.h", flist, env:generator(editor_theme_builders.make_fonts_header))
    env:sources(env.editor_sources, "*.cpp")
end
