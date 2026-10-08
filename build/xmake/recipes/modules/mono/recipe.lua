-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_mono, mono_configure
    mono_configure = graph:mono_configure()
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_mono = env_modules:clone()
    mono_configure.configure(env, env_mono)
    env_mono:sources(env.modules_sources, "*.cpp")
    env_mono:sources(env.modules_sources, "glue/*.cpp")
    env_mono:sources(env.modules_sources, "mono_gd/*.cpp")
    env_mono:sources(env.modules_sources, "utils/*.cpp")
    if R.truthy(env.editor_build) then
        env_mono:sources(env.modules_sources, "editor/*.cpp")
        graph:include("editor/script_templates/recipe.lua")
    end
end
