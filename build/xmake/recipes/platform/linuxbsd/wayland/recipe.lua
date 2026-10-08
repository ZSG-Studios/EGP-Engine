-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, generate_from_xml, generated_sources, source_files
    env = graph:use("env")
    generate_from_xml = function(name, protocol)
        local inputs = {graph:file(protocol), env:value(env.use_sowrap), graph:file("#build/xmake/generators/wayland.lua")}
        env:generate("protocol/" .. name .. ".gen.h", inputs, "wayland.scanner.client_header")
        return env:generate("protocol/" .. name .. ".gen.c", inputs, "wayland.scanner.private_code")
    end
    generated_sources = {generate_from_xml("wayland", "#thirdparty/wayland/protocol/wayland.xml"), generate_from_xml("tablet", "#thirdparty/wayland-protocols/stable/tablet/tablet-v2.xml"), generate_from_xml("viewporter", "#thirdparty/wayland-protocols/stable/viewporter/viewporter.xml"), generate_from_xml("xdg_shell", "#thirdparty/wayland-protocols/stable/xdg-shell/xdg-shell.xml"), generate_from_xml("color_management", "#thirdparty/wayland-protocols/staging/color-management/color-management-v1.xml"), generate_from_xml("cursor_shape", "#thirdparty/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml"), generate_from_xml("fractional_scale", "#thirdparty/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml"), generate_from_xml("xdg_activation", "#thirdparty/wayland-protocols/staging/xdg-activation/xdg-activation-v1.xml"), generate_from_xml("xdg_system_bell", "#thirdparty/wayland-protocols/staging/xdg-system-bell/xdg-system-bell-v1.xml"), generate_from_xml("xdg_toplevel_icon", "#thirdparty/wayland-protocols/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml"), generate_from_xml("pointer_warp", "#thirdparty/wayland-protocols/staging/pointer-warp/pointer-warp-v1.xml"), generate_from_xml("idle_inhibit", "#thirdparty/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml"), generate_from_xml("pointer_constraints", "#thirdparty/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml"), generate_from_xml("pointer_gestures", "#thirdparty/wayland-protocols/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml"), generate_from_xml("primary_selection", "#thirdparty/wayland-protocols/unstable/primary-selection/primary-selection-unstable-v1.xml"), generate_from_xml("relative_pointer", "#thirdparty/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml"), generate_from_xml("text_input", "#thirdparty/wayland-protocols/unstable/text-input/text-input-unstable-v3.xml"), generate_from_xml("xdg_decoration", "#thirdparty/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml"), generate_from_xml("xdg_foreign_v1", "#thirdparty/wayland-protocols/unstable/xdg-foreign/xdg-foreign-unstable-v1.xml"), generate_from_xml("xdg_foreign_v2", "#thirdparty/wayland-protocols/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml"), generate_from_xml("linux_dmabuf_v1", "#thirdparty/wayland-protocols/stable/linux-dmabuf/linux-dmabuf-v1.xml"), generate_from_xml("linux_explicit_synchronization_unstable_v1", "#thirdparty/wayland-protocols/unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml"), generate_from_xml("fifo_v1", "#thirdparty/wayland-protocols/staging/fifo/fifo-v1.xml"), generate_from_xml("commit_timing_v1", "#thirdparty/wayland-protocols/staging/commit-timing/commit-timing-v1.xml"), generate_from_xml("linux_drm_syncobj_v1", "#thirdparty/wayland-protocols/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml"), generate_from_xml("tearing_control_v1", "#thirdparty/wayland-protocols/staging/tearing-control/tearing-control-v1.xml"), generate_from_xml("wayland-drm", "#thirdparty/wayland-protocols/mesa/wayland-drm.xml"), generate_from_xml("godot_embedding_compositor", "godot-embedding-compositor.xml")}
    source_files = R.add(generated_sources, {graph:file("detect_prime_egl.cpp"), graph:file("display_server_wayland.cpp"), graph:file("key_mapping_xkb.cpp"), graph:file("wayland_thread.cpp"), graph:file("wayland_embedder.cpp")})
    if R.truthy(R.index(env, "use_sowrap")) then
        R.append(source_files, {graph:file("dynwrappers/wayland-cursor-so_wrap.c"), graph:file("dynwrappers/wayland-client-core-so_wrap.c"), graph:file("dynwrappers/wayland-egl-core-so_wrap.c")})
        if R.truthy(R.index(env, "libdecor")) then
            R.append(source_files, graph:file("dynwrappers/libdecor-so_wrap.c"))
        end
    end
    if R.truthy(R.index(env, "vulkan")) then
        R.append(source_files, graph:file("rendering_context_driver_vulkan_wayland.cpp"))
    end
    if R.truthy(R.index(env, "opengl3")) then
        R.append(source_files, graph:file("egl_manager_wayland.cpp"))
        R.append(source_files, graph:file("egl_manager_wayland_gles.cpp"))
    end
    return source_files
end
