-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local build_template_gd, env
    env = graph:use("env")
    build_template_gd = graph:builders("editor.template_builders")
    env:generate("templates.gen.h", graph:files("*/*.gd"), env:generator(build_template_gd.make_templates))
end
