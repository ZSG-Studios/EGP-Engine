-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_openxr, module_obj
    env = graph:use("env")
    env_openxr = graph:use("env_openxr")
    module_obj = {}
    env_openxr:sources(module_obj, "*.cpp")
    env_openxr:sources(module_obj, "spatial_container/*.cpp")
    env_openxr:sources(module_obj, "spatial_entities/*.cpp")
    if R.truthy(((R.index(env, "platform") == "android"))) then
        env_openxr:sources(module_obj, "platform/openxr_android_extension.cpp")
    end
    if R.truthy(R.index(env, "vulkan")) then
        env_openxr:sources(module_obj, "platform/openxr_vulkan_extension.cpp")
    end
    if R.truthy(R.index(env, "metal")) then
        env_openxr:sources(module_obj, "platform/openxr_metal_extension.mm")
    end
    if R.truthy((function() local v = R.index(env, "opengl3"); if not R.truthy(v) then return v end; return ((R.index(env, "platform") ~= "macos")) end)()) then
        env_openxr:sources(module_obj, "platform/openxr_opengl_extension.cpp")
    end
    if R.truthy(R.index(env, "d3d12")) then
        env_openxr:sources(module_obj, "platform/openxr_d3d12_extension.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
end
