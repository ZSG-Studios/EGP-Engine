-- Native Apple embedded platform archive. Packaging merges engine archives after linking.
function main(graph)
    local env = graph:use("env"):clone()
    local swift_files = {}
    for _, source in ipairs(graph:files("*.swift")) do table.insert(swift_files, source.name) end
    table.insert(swift_files, "../../drivers/apple_embedded/godot_swiftui_view_controller.swift")
    local swift_sources = graph:methods().setup_swift_builder(env, env.APPLE_PLATFORM, env.APPLE_SDK_PATH,
        graph.current, "bridging_header_ios.h", swift_files)
    env:add({CCFLAGS = {"-fmodules", "-fcxx-modules"}})
    local sources = graph:files("*.mm")
    table.join2(sources, swift_sources)
    env:program("#bin/libgodot", sources)
    graph.programs[#graph.programs].kind = "static"
end
