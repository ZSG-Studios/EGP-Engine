-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_etcpak, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_etcpak = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/etcpak/"
    thirdparty_sources = {"DecodeRGB.cpp", "Dither.cpp", "ProcessDxtc.cpp", "ProcessRGB.cpp", "Tables.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_etcpak:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_thirdparty = env_etcpak:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    if R.truthy((function() local v = env.editor_build; if R.truthy(v) then return v end; return R.index(env, "etcpak_export_templates") end)()) then
        env_etcpak:add({["CPPDEFINES"] = {"ETCPAK_COMPRESS_ENABLED"}})
    end
    env_etcpak:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
