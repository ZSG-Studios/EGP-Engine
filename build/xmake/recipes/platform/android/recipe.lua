-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local android_files, android_objects, env, env_android, env_thirdparty, lib, thirdparty_obj, x
    env = graph:use("env")
    android_files = {"os_android.cpp", "android_input_handler.cpp", "file_access_android.cpp", "file_access_filesystem_jandroid.cpp", "audio_driver_opensl.cpp", "dir_access_jandroid.cpp", "tts_android.cpp", "thread_jandroid.cpp", "net_socket_android.cpp", "java_godot_lib_jni.cpp", "java_class_wrapper.cpp", "java_godot_wrapper.cpp", "java_godot_view_wrapper.cpp", "java_godot_io_wrapper.cpp", "jni_utils.cpp", "android_keys_utils.cpp", "display_server_android.cpp", "plugin/godot_plugin_jni.cpp", "rendering_context_driver_vulkan_android.cpp", "variant/callable_jni.cpp", "dialog_utils_jni.cpp", "editor/game_menu_utils_jni.cpp", "editor/editor_utils_jni.cpp"}
    env_android = env:clone()
    android_objects = {}
    for _, __item1 in ipairs(R.iter(android_files)) do
        x = __item1
        R.append(android_objects, env_android:object(x))
    end
    env_thirdparty = env_android:clone()
    env_thirdparty:disable_warnings()
    thirdparty_obj = env_thirdparty:object("#thirdparty/misc/ifaddrs-android.cc")
    R.append(android_objects, thirdparty_obj)
    lib = env_android:shared_library("#bin/libgodot", android_objects, {["redirect_build_objects"] = false})
    env:depends(lib, thirdparty_obj)
    -- APK/AAR staging, symbols and Gradle execute through native post-link packaging.
end
