-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local controller_databases, env, gensource, input_builders
    env = graph:use("env")
    input_builders = graph:builders("input_builders")
    controller_databases = {"gamecontrollerdb.txt", "godotcontrollerdb.txt"}
    gensource = env:generate("default_controller_mappings.gen.cpp", controller_databases, env:generator(input_builders.make_default_controller_mappings))
    env:sources(env.core_sources, "*.cpp")
    env:sources(env.core_sources, gensource)
end
