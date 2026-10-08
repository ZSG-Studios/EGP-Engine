-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_thirdparty, thirdparty_dir, thirdparty_sources
    env = graph:use("env")
    if R.truthy((R.contains({"macos", "windows", "linuxbsd"}, R.index(env, "platform")))) then
        thirdparty_dir = "#thirdparty/glad/"
        thirdparty_sources = {"gl.c"}
        env:prepend({["CPPPATH"] = {thirdparty_dir}})
        env:add({["CPPDEFINES"] = {"GLAD_ENABLED"}})
        if R.truthy(((R.index(env, "platform") == "linuxbsd"))) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"egl.c"})
            env:add({["CPPDEFINES"] = {"EGL_ENABLED", "GLAD_GLES2"}})
        else
            if R.truthy(R.index(env, "angle")) then
                env:add({["CPPDEFINES"] = {"EGL_ENABLED", "GLAD_GLES2"}})
            end
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_thirdparty = env:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(env.drivers_sources, thirdparty_sources)
    end
    env:sources(env.drivers_sources, "*.cpp")
end
