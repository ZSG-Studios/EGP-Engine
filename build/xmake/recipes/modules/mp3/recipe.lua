-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_mp3
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_mp3 = env_modules:clone()
    if R.truthy(not R.truthy(R.index(env, "mp3_extra_formats"))) then
        env_mp3:add({["CPPDEFINES"] = {"DR_MP3_ONLY_MP3"}})
    end
    env_mp3:sources(env.modules_sources, "*.cpp")
    if R.truthy(env.editor_build) then
        env_mp3:sources(env.modules_sources, "editor/*.cpp")
    end
end
