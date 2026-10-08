-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_main, lib, main_builders
    env = graph:use("env")
    main_builders = graph:builders("main_builders")
    env.main_sources = {}
    env_main = env:clone()
    env_main:sources(env.main_sources, "*.cpp")
    if R.truthy((function() local v = R.index(env, "steamapi"); if not R.truthy(v) then return v end; return env.editor_build end)()) then
        env_main:add({["CPPDEFINES"] = {"STEAMAPI_ENABLED"}})
    end
    if R.truthy(R.index(env, "tests")) then
        env_main:add({["CPPDEFINES"] = {"TESTS_ENABLED"}})
    end
    env_main:generate("#main/splash.gen.h", "#main/splash.png", env:generator(main_builders.make_splash))
    if R.truthy((function() local v = env_main.editor_build; if not R.truthy(v) then return v end; return not R.truthy(R.index(env_main, "no_editor_splash")) end)()) then
        env_main:generate("#main/splash_editor.gen.h", "#main/splash_editor.png", env:generator(main_builders.make_splash_editor))
    end
    env_main:generate("#main/app_icon.gen.h", "#main/app_icon.png", env:generator(main_builders.make_app_icon))
    lib = env_main:library("main", env.main_sources)
    env:prepend({["LIBS"] = {lib}})
end
