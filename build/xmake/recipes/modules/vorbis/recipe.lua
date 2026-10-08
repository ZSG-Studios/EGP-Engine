-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_vorbis, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_vorbis = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_libvorbis")) then
        thirdparty_dir = "#thirdparty/libvorbis/"
        thirdparty_sources = {"bitrate.c", "block.c", "codebook.c", "envelope.c", "floor0.c", "floor1.c", "info.c", "lookup.c", "lpc.c", "lsp.c", "mapping0.c", "mdct.c", "psy.c", "registry.c", "res0.c", "sharedbook.c", "smallft.c", "synthesis.c", "vorbisfile.c", "window.c"}
        if R.truthy(env.editor_build) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"analysis.c", "vorbisenc.c"})
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_vorbis:prepend({["CPPPATH"] = {thirdparty_dir}})
        if R.truthy(R.index(env, "builtin_libogg")) then
            env_vorbis:prepend({["CPPPATH"] = {"#thirdparty/libogg"}})
        end
        env_thirdparty = env_vorbis:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_vorbis:sources(module_obj, "*.cpp")
    if R.truthy(env.editor_build) then
        env_vorbis:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
