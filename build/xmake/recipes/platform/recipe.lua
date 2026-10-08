-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, glob, lib, path, platform, platform_builders, register_platform_apis
    glob = R.glob
    platform_builders = graph:builders("platform_builders")
    env = graph:use("env")
    env.platform_sources = {}
    for _, __item1 in ipairs(R.iter(env.platform_exporters)) do
        platform = __item1
        for _, __item2 in ipairs(R.iter(R.glob(R.join({R.str(platform), "/export/*.svg"}, "")))) do
            path = __item2
            env:generate(R.replace(path, ".svg", "_svg.gen.h"), path, env:generator(platform_builders.export_icon_builder))
        end
    end
    register_platform_apis = env:generate("register_platform_apis.gen.cpp", env:value(env.platform_apis), env:generator(platform_builders.register_platform_apis_builder))
    env:sources(env.platform_sources, register_platform_apis)
    for _, __item3 in ipairs(R.iter(env.platform_apis)) do
        platform = __item3
        env:sources(env.platform_sources, R.join({R.str(platform), "/api/*.cpp"}, ""))
    end
    lib = env:library("platform", env.platform_sources)
    env:prepend({["LIBS"] = {lib}})
end
