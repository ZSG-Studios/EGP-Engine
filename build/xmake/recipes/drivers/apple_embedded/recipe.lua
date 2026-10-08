-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local apple_platform, bridging_header_filename, current_path, env, env_apple_embedded, files, sdk_path, setup_swift_builder, swift_file_names, swift_files, vulkan_dir
    files = function(...) return graph:files(...) end
    setup_swift_builder = graph:builders("platform_methods").setup_swift_builder
    env = graph:use("env")
    env_apple_embedded = env:clone()
    env_apple_embedded:add({["CCFLAGS"] = {"-fmodules", "-fcxx-modules"}})
    apple_platform = R.index(env, "APPLE_PLATFORM")
    sdk_path = R.index(env, "APPLE_SDK_PATH")
    current_path = graph:directory(".").abspath
    bridging_header_filename = "bridging_header_apple_embedded.h"
    swift_files = graph:files("*.swift")
    swift_file_names = R.list(R.map(function(f) return f.name end, swift_files))
    -- Shared Swift source is compiled once with the native platform module.
    vulkan_dir = "#thirdparty/vulkan"
    env_apple_embedded:prepend({["CPPPATH"] = {vulkan_dir, R.add(vulkan_dir, "/include")}})
    env_apple_embedded:prepend({["CPPPATH"] = {"#thirdparty/metal-cpp"}})
    env_apple_embedded:sources(env_apple_embedded.drivers_sources, "*.mm")
end
