-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_text_server_fb, freetype_enabled, msdfgen_enabled
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    freetype_enabled = (R.contains(env.module_list, "freetype"))
    msdfgen_enabled = (R.contains(env.module_list, "msdfgen"))
    env_text_server_fb = env_modules:clone()
    if R.truthy((R.contains(env.module_list, "svg"))) then
        env_text_server_fb:prepend({["CPPPATH"] = {"#thirdparty/thorvg/inc", "#thirdparty/thorvg/src/common", "#thirdparty/thorvg/src/renderer"}})
        env_text_server_fb:add({["CPPDEFINES"] = {"TVG_STATIC"}})
    end
    if R.truthy((function() local v = R.index(env, "builtin_msdfgen"); if not R.truthy(v) then return v end; return msdfgen_enabled end)()) then
        env_text_server_fb:add({["CPPDEFINES"] = {{"MSDFGEN_PUBLIC", ""}}})
        env_text_server_fb:prepend({["CPPPATH"] = {"#thirdparty/msdfgen"}})
    end
    if R.truthy((function() local v = R.index(env, "builtin_freetype"); if not R.truthy(v) then return v end; return freetype_enabled end)()) then
        env_text_server_fb:add({["CPPDEFINES"] = {"FT_CONFIG_OPTION_USE_BROTLI"}})
        env_text_server_fb:prepend({["CPPPATH"] = {"#thirdparty/freetype/include"}})
    end
    env_text_server_fb:sources(env.modules_sources, "*.cpp")
end
