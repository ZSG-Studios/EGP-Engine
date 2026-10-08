-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, file, source_files
    env = graph:use("env")
    file = env.file
    source_files = {graph:file("display_server_x11.cpp"), graph:file("key_mapping_x11.cpp")}
    if R.truthy(R.index(env, "use_sowrap")) then
        R.append(source_files, {graph:file("dynwrappers/xlib-so_wrap.c"), graph:file("dynwrappers/xcursor-so_wrap.c"), graph:file("dynwrappers/xinerama-so_wrap.c"), graph:file("dynwrappers/xinput2-so_wrap.c"), graph:file("dynwrappers/xrandr-so_wrap.c"), graph:file("dynwrappers/xrender-so_wrap.c"), graph:file("dynwrappers/xext-so_wrap.c")})
    end
    if R.truthy(R.index(env, "vulkan")) then
        R.append(source_files, graph:file("rendering_context_driver_vulkan_x11.cpp"))
    end
    return source_files
end
