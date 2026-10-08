-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local build_template_cs, env
    env = graph:use("env")
    build_template_cs = graph:builders("editor.template_builders")
    env:generate("templates.gen.h", graph:files("*/*.cs"), env:generator(build_template_cs.make_templates))
end
