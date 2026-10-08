-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_upnp, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_upnp = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy((function() local v = R.index(env, "builtin_miniupnpc"); if not R.truthy(v) then return v end; return ((R.index(env, "platform") ~= "web")) end)()) then
        thirdparty_dir = "#thirdparty/miniupnpc/"
        thirdparty_sources = {"igd_desc_parse.c", "miniupnpc.c", "minixml.c", "minisoap.c", "minissdpc.c", "miniwget.c", "upnpcommands.c", "upnpdev.c", "upnpreplyparse.c", "connecthostport.c", "portlistingparse.c", "receivedata.c", "addr_is_reserved.c"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(R.add(thirdparty_dir, "src/"), file)); end; return __item1 end)()
        env_upnp:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "include")}})
        env_upnp:add({["CPPDEFINES"] = {"MINIUPNP_STATICLIB"}})
        if R.truthy(((R.index(env, "platform") ~= "windows"))) then
            env_upnp:add({["CPPDEFINES"] = {"MINIUPNPC_SET_SOCKET_TIMEOUT"}})
        end
        env_thirdparty = env_upnp:clone()
        env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "include/miniupnpc")}})
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_upnp:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
