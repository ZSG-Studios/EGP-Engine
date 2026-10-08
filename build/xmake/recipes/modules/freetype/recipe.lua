-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_freetype, env_modules, env_thirdparty, idx, inserted, lib, linklib, module_obj, sfnt, thirdparty_dir, thirdparty_obj, thirdparty_sources, tmp_env
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_freetype = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_freetype")) then
        thirdparty_dir = "#thirdparty/freetype/"
        thirdparty_sources = {"src/autofit/autofit.c", "src/base/ftbase.c", "src/base/ftbbox.c", "src/base/ftbdf.c", "src/base/ftbitmap.c", "src/base/ftcid.c", "src/base/ftdebug.c", "src/base/ftfstype.c", "src/base/ftgasp.c", "src/base/ftglyph.c", "src/base/ftgxval.c", "src/base/ftinit.c", "src/base/ftmm.c", "src/base/ftotval.c", "src/base/ftpatent.c", "src/base/ftpfr.c", "src/base/ftstroke.c", "src/base/ftsynth.c", "src/base/ftsystem.c", "src/base/fttype1.c", "src/base/ftwinfnt.c", "src/bdf/bdf.c", "src/bzip2/ftbzip2.c", "src/cache/ftcache.c", "src/cff/cff.c", "src/cid/type1cid.c", "src/gxvalid/gxvalid.c", "src/gzip/ftgzip.c", "src/lzw/ftlzw.c", "src/otvalid/otvalid.c", "src/pcf/pcf.c", "src/pfr/pfr.c", "src/psaux/psaux.c", "src/pshinter/pshinter.c", "src/psnames/psnames.c", "src/raster/raster.c", "src/sdf/sdf.c", "src/svg/svg.c", "src/smooth/smooth.c", "src/truetype/truetype.c", "src/type1/type1.c", "src/type42/type42.c", "src/winfonts/winfnt.c"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        if R.truthy(R.index(env, "brotli")) then
            env_freetype:add({["CPPDEFINES"] = {"FT_CONFIG_OPTION_USE_BROTLI"}})
        end
        env_freetype:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "/include")}})
        env:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "/include")}})
        env_freetype:add({["CPPDEFINES"] = {"FT2_BUILD_LIBRARY", "FT_CONFIG_OPTION_USE_PNG", "FT_CONFIG_OPTION_SYSTEM_ZLIB"}})
        if R.truthy(R.index(env, "builtin_libpng")) then
            env_freetype:prepend({["CPPPATH"] = {"#thirdparty/libpng"}})
        end
        if R.truthy((R.contains(env.module_list, "text_server_adv"))) then
            env_freetype:add({["CPPDEFINES"] = {"FT_CONFIG_OPTION_USE_HARFBUZZ"}})
            if R.truthy(R.index(env, "builtin_harfbuzz")) then
                env_freetype:prepend({["CPPPATH"] = {"#thirdparty/harfbuzz/src/"}})
            end
        end
        sfnt = R.add(thirdparty_dir, "src/sfnt/sfnt.c")
        if R.truthy(((R.index(env, "platform") == "web"))) then
            tmp_env = env_freetype:clone()
            tmp_env:disable_warnings()
            tmp_env:add({["CPPFLAGS"] = {"-U__OPTIMIZE__"}})
            sfnt = tmp_env:object(sfnt)
        end
        thirdparty_sources = R.iadd(thirdparty_sources, {sfnt})
        env_thirdparty = env_freetype:clone()
        env_thirdparty:disable_warnings()
        lib = env_thirdparty:library("freetype_builtin", thirdparty_sources)
        thirdparty_obj = R.iadd(thirdparty_obj, lib)
        inserted = false
        for _, __item3 in ipairs(R.iter(R.enumerate(R.index(env, "LIBS")))) do
            local __item4 = __item3; idx = R.index(__item4, 0); linklib = R.index(__item4, 1)
            if R.truthy(R.isinstance(linklib, {"str", "bytes"})) then
                R.insert(R.index(env, "LIBS"), idx, lib)
                inserted = true
                break
            end
        end
        if R.truthy(not R.truthy(inserted)) then
            env:add({["LIBS"] = {lib}})
        end
    end
    module_obj = {}
    env_freetype:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
