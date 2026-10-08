-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    if R.truthy(R.index(env, "vulkan")) then
        env:sources(env.editor_sources, "shader_baker_export_plugin_platform_vulkan.cpp")
    end
    if R.truthy(R.index(env, "d3d12")) then
        env:sources(env.editor_sources, "shader_baker_export_plugin_platform_d3d12.cpp")
    end
    if R.truthy(R.index(env, "metal")) then
        env:sources(env.editor_sources, "shader_baker_export_plugin_platform_metal.cpp")
    end
end
