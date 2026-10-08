-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local add_bit_depth, env, env_jpg, env_modules, env_thirdparty, module_obj, source_paths, thirdparty_dir, thirdparty_obj, thirdparty_sources_bit_dependent, thirdparty_sources_by_bits, thirdparty_sources_common
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_jpg = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_libjpeg_turbo")) then
        thirdparty_dir = "#thirdparty/libjpeg-turbo"
        thirdparty_sources_common = {"jaricom.c", "jcapimin.c", "jcarith.c", "jchuff.c", "jcicc.c", "jcinit.c", "jcmarker.c", "jcmaster.c", "jcomapi.c", "jcparam.c", "jcphuff.c", "jctrans.c", "jdapimin.c", "jdarith.c", "jdatadst.c", "jdatadst-tj.c", "jdatasrc.c", "jdatasrc-tj.c", "jdhuff.c", "jdicc.c", "jdinput.c", "jdmarker.c", "jdmaster.c", "jdphuff.c", "jdtrans.c", "jerror.c", "jfdctflt.c", "jmemmgr.c", "jmemnobs.c", "jpeg_nbits.c", "transupp.c", "turbojpeg.c"}
        thirdparty_sources_bit_dependent = {"jcapistd.c", "jccoefct.c", "jccolor.c", "jcdctmgr.c", "jcmainct.c", "jcprepct.c", "jcsample.c", "jdcoefct.c", "jdcolor.c", "jdapistd.c", "jddctmgr.c", "jdmainct.c", "jdmerge.c", "jdpostct.c", "jdsample.c", "jfdctfst.c", "jfdctint.c", "jidctflt.c", "jidctfst.c", "jidctint.c", "jidctred.c", "jutils.c", "jquant1.c", "jquant2.c"}
        thirdparty_sources_by_bits = R.dict({[8] = R.list(thirdparty_sources_bit_dependent), [12] = R.list(thirdparty_sources_bit_dependent)})
        source_paths = function(files)
            return (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(files)) do; local f = __item2; table.insert(__item1, R.add(R.add(thirdparty_dir, "/src/"), f)); end; return __item1 end)()
        end
        env_jpg:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "/src")}})
        add_bit_depth = function(bit_depth)
            local env_bit_depth
            env_bit_depth = env_jpg:clone()
            env_bit_depth:disable_warnings()
            R.setindex(env_bit_depth, "OBJSUFFIX", R.join({"_", R.str(bit_depth), R.str(R.index(env_bit_depth, "OBJSUFFIX"))}, ""))
            env_bit_depth:add({["CPPDEFINES"] = {R.join({"BITS_IN_JSAMPLE=", R.str(bit_depth)}, "")}})
            env_bit_depth:sources(thirdparty_obj, source_paths(R.index(thirdparty_sources_by_bits, bit_depth)))
        end
        add_bit_depth(8)
        add_bit_depth(12)
        env_thirdparty = env_jpg:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, source_paths(thirdparty_sources_common))
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_jpg:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
