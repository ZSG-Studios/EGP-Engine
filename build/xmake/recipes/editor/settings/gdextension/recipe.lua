-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, native_extension_sdk, sdk_env, sdk_sources
    env = graph:use("env")
    native_extension_sdk = graph:builders("native_extension_sdk")
    sdk_env = env:clone()
    R.setindex(sdk_env, "egp_cpp_bits", (function() if R.truthy((R.contains({"x86_32", "arm32", "rv32", "wasm32"}, R.index(env, "arch")))) then return "32" else return "64" end end)())
    R.setindex(sdk_env, "egp_cpp_api", R.get(graph.options, "egp_cpp_api"))
    sdk_sources = {graph:file("cpp_sdk/xmake.lua"), graph:file("#build/xmake/generated_objects.lua")}
    sdk_sources = R.iadd(sdk_sources, graph:files("#build/xmake/generators/*.lua"))
    sdk_sources = R.iadd(sdk_sources, graph:files("#thirdparty/godot-cpp/include/godot_cpp/*/*"))
    sdk_sources = R.iadd(sdk_sources, graph:files("#thirdparty/godot-cpp/include/godot_cpp/*.hpp"))
    sdk_sources = R.iadd(sdk_sources, graph:files("#thirdparty/godot-cpp/src/*.cpp"))
    sdk_sources = R.iadd(sdk_sources, graph:files("#thirdparty/godot-cpp/src/*/*.cpp"))
    sdk_sources = R.iadd(sdk_sources, graph:files("#thirdparty/godot-cpp/gdextension/*"))
    sdk_sources = R.iadd(sdk_sources, graph:files("cpp_sdk/templates/*"))
    sdk_sources = R.iadd(sdk_sources, graph:files("cpp_sdk/tools/*"))
    sdk_sources = R.iadd(sdk_sources, {graph:file("#thirdparty/godot-cpp/LICENSE.md"), env:value(R.index(env, "precision")), env:value(R.index(sdk_env, "egp_cpp_bits"))})
    if R.truthy(R.index(sdk_env, "egp_cpp_api")) then
        sdk_sources = R.iadd(sdk_sources, {graph:file(R.index(sdk_env, "egp_cpp_api"))})
    end
    sdk_env:generate("native_extension_sdk.gen.h", sdk_sources, sdk_env:generator(native_extension_sdk.build_header))
    env:sources(env.editor_sources, "*.cpp")
end
