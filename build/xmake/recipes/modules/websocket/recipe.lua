-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_ws, module_obj, sources, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_ws = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(((R.index(env, "platform") == "web"))) then
        env:AddJSLibraries({"library_godot_websocket.js"})
    else
        if R.truthy(R.index(env, "builtin_wslay")) then
            thirdparty_dir = "#thirdparty/wslay/"
            thirdparty_sources = {"wslay_net.c", "wslay_event.c", "wslay_queue.c", "wslay_frame.c"}
            thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local s = __item2; table.insert(__item1, R.add(thirdparty_dir, s)); end; return __item1 end)()
            env_ws:prepend({["CPPPATH"] = {thirdparty_dir}})
            env_ws:add({["CPPDEFINES"] = {"HAVE_CONFIG_H"}})
            if R.truthy(((R.index(env, "platform") == "windows"))) then
                env_ws:add({["CPPDEFINES"] = {"HAVE_WINSOCK2_H"}})
            else
                env_ws:add({["CPPDEFINES"] = {"HAVE_NETINET_IN_H"}})
            end
            env_thirdparty = env_ws:clone()
            env_thirdparty:disable_warnings()
            env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
            env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
        end
    end
    module_obj = {}
    sources = env_ws:files("*.cpp")
    env_ws:sources(module_obj, sources)
    if R.truthy(env.editor_build) then
        env_ws:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
