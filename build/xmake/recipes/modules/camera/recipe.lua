-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_camera, env_modules, ext_camera_lib, ext_module_source
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_camera = env_modules:clone()
    if R.truthy((R.contains({"windows", "macos", "linuxbsd", "android", "ios", "visionos"}, R.index(env, "platform")))) then
        env_camera:sources(env.modules_sources, "register_types.cpp")
    end
    if R.truthy(((R.index(env, "platform") == "windows"))) then
        env_camera:sources(env.modules_sources, "camera_win.cpp")
    else
        if R.truthy(((R.index(env, "platform") == "macos"))) then
            env_camera:sources(env.modules_sources, "camera_apple.mm")
        else
            if R.truthy(((R.index(env, "platform") == "android"))) then
                env_camera:sources(env.modules_sources, "camera_android.cpp")
                env:add({["LIBS"] = {"camera2ndk", "mediandk"}})
            else
                if R.truthy((R.contains({"ios", "visionos"}, R.index(env, "platform")))) then
                    ext_module_source = {"camera_apple.mm"}
                    ext_camera_lib = env_camera:library("#bin/libgodot_camera", ext_module_source)
                    env:add({["LIBS_EXTERNAL"] = {ext_camera_lib}})
                    env:add({["MODULES_EXTERNAL"] = {"_camera"}})
                else
                    if R.truthy(((R.index(env, "platform") == "linuxbsd"))) then
                        env_camera:sources(env.modules_sources, "camera_linux.cpp")
                        env_camera:sources(env.modules_sources, "camera_feed_linux.cpp")
                        env_camera:sources(env.modules_sources, "buffer_decoder.cpp")
                    end
                end
            end
        end
    end
end
