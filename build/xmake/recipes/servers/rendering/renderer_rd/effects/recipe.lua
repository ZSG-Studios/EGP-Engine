-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local areatex_builder, env, env_effects, env_thirdparty, methods, module_obj, searchtex_builder, thirdparty_dir, thirdparty_obj, thirdparty_sources
    methods = graph:methods()
    env = graph:use("env")
    env_effects = env:clone()
    if R.truthy(R.index(env, "metal")) then
        env_effects:prepend({["CPPPATH"] = {"#thirdparty/metal-cpp"}})
    end
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/amd-fsr2/"
    thirdparty_sources = {"ffx_assert.cpp", "ffx_fsr2.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_effects:prepend({["CPPPATH"] = {thirdparty_dir}})
    areatex_builder = "servers/rendering/renderer_rd/effects/recipe:areatex_builder"
    env:generate("smaa_area_tex.gen.h", "#thirdparty/smaa/AreaTex.png", env:generator(areatex_builder))
    searchtex_builder = "servers/rendering/renderer_rd/effects/recipe:searchtex_builder"
    env:generate("smaa_search_tex.gen.h", "#thirdparty/smaa/SearchTex.png", env:generator(searchtex_builder))
    env_effects:add({["CPPDEFINES"] = {"FFX_GCC"}})
    env_thirdparty = env_effects:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.servers_sources = R.iadd(env.servers_sources, thirdparty_obj)
    module_obj = {}
    env_effects:sources(module_obj, "*.cpp")
    env.servers_sources = R.iadd(env.servers_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
