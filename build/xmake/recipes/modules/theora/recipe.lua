-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_theora, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources, thirdparty_sources_x86, thirdparty_sources_x86_vc
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_theora = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_libtheora")) then
        thirdparty_dir = "#thirdparty/libtheora/"
        thirdparty_sources = {"bitpack.c", "decinfo.c", "decode.c", "dequant.c", "fragment.c", "huffdec.c", "idct.c", "info.c", "internal.c", "quant.c", "state.c"}
        if R.truthy(env.editor_build) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"analyze.c", "encfrag.c", "encinfo.c", "encode.c", "enquant.c", "fdct.c", "huffenc.c", "mathops.c", "mcenc.c", "rate.c", "tokenize.c"})
        end
        thirdparty_sources_x86 = {"x86/mmxfrag.c", "x86/mmxidct.c", "x86/mmxstate.c", "x86/sse2idct.c", "x86/x86cpu.c", "x86/x86state.c"}
        if R.truthy(env.editor_build) then
            thirdparty_sources_x86 = R.iadd(thirdparty_sources_x86, {"x86/mmxencfrag.c", "x86/mmxfdct.c", "x86/sse2encfrag.c", "x86/sse2fdct.c", "x86/x86enc.c", "x86/x86enquant.c"})
        end
        thirdparty_sources_x86_vc = {"x86_vc/mmxfrag.c", "x86_vc/mmxidct.c", "x86_vc/mmxstate.c", "x86_vc/x86cpu.c", "x86_vc/x86state.c"}
        if R.truthy(env.editor_build) then
            thirdparty_sources_x86_vc = R.iadd(thirdparty_sources_x86_vc, {"x86_vc/mmxencfrag.c", "x86_vc/mmxfdct.c", "x86_vc/x86enc.c"})
        end
        if R.truthy(R.index(env, "x86_libtheora_opt_gcc")) then
            thirdparty_sources = R.iadd(thirdparty_sources, thirdparty_sources_x86)
        end
        if R.truthy(R.index(env, "x86_libtheora_opt_vc")) then
            thirdparty_sources = R.iadd(thirdparty_sources, thirdparty_sources_x86_vc)
        end
        if R.truthy((function() local v = R.index(env, "x86_libtheora_opt_gcc"); if R.truthy(v) then return v end; return R.index(env, "x86_libtheora_opt_vc") end)()) then
            env_theora:add({["CPPDEFINES"] = {"OC_X86_ASM"}})
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_theora:prepend({["CPPPATH"] = {thirdparty_dir}})
        if R.truthy(R.index(env, "builtin_libogg")) then
            env_theora:prepend({["CPPPATH"] = {"#thirdparty/libogg"}})
        end
        if R.truthy(R.index(env, "builtin_libvorbis")) then
            env_theora:prepend({["CPPPATH"] = {"#thirdparty/libvorbis"}})
        end
        env_thirdparty = env_theora:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_theora:sources(module_obj, "*.cpp")
    if R.truthy(env.editor_build) then
        env_theora:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
