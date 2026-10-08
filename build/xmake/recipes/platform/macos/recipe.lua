-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, files, platform_macos_builders, prog
    platform_macos_builders = graph:builders("platform_macos_builders")
    env = graph:use("env")
    files = {"os_macos.mm", "godot_application.mm", "godot_application_delegate.mm", "crash_handler_macos.mm", "display_server_macos_base.mm", "display_server_macos.mm", "godot_button_view.mm", "godot_content_view.mm", "godot_core_cursor.mm", "godot_status_item.mm", "godot_window_delegate.mm", "godot_window.mm", "key_mapping_macos.mm", "godot_menu_delegate.mm", "godot_menu_item.mm", "godot_open_save_delegate.mm", "native_menu_macos.mm", "dir_access_macos.mm", "tts_macos.mm", "rendering_context_driver_vulkan_macos.mm", "godot_progress_view.mm"}
    if R.truthy(env.editor_build) then
        files = R.iadd(files, {"display_server_macos_embedded.mm", "embedded_debugger.mm", "editor/embedded_game_view_plugin.mm", "editor/embedded_process_macos.mm"})
    end
    if R.truthy(((R.index(env, "library_type") == "executable"))) then
        files = R.iadd(files, {"godot_main_macos.mm"})
    else
        files = R.iadd(files, {"libgodot_macos.mm"})
    end
    if R.truthy(((R.index(env, "library_type") == "static_library"))) then
        prog = env:library("#bin/godot", files)
    else
        if R.truthy(((R.index(env, "library_type") == "shared_library"))) then
            prog = env:shared_library("#bin/godot", files)
        else
            prog = env:program("#bin/godot", files)
        end
    end
    if R.truthy((function() local v = R.index(env, "debug_symbols"); if not R.truthy(v) then return v end; return R.index(env, "separate_debug_symbols") end)()) then
        env:after_build(prog, env:generator(platform_macos_builders.make_debug_macos))
    end
    if R.truthy(R.index(env, "generate_bundle")) then
        env:AlwaysBuild(env:generate("generate_bundle", prog, env:generator(platform_macos_builders.generate_bundle)))
    end
end
