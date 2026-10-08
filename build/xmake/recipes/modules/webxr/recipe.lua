-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_webxr
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    if R.truthy(((R.index(env, "platform") == "web"))) then
        env:AddJSLibraries({"native/library_godot_webxr.js"})
        env:AddJSExterns({"native/webxr.externs.js"})
    end
    env_webxr = env_modules:clone()
    env_webxr:sources(env.modules_sources, "*.cpp")
end
