-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local driver_obj, env, env_neon, env_png, env_thirdparty, neon_sources, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_png = env:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_libpng")) then
        thirdparty_dir = "#thirdparty/libpng/"
        thirdparty_sources = {"png.c", "pngerror.c", "pngget.c", "pngmem.c", "pngpread.c", "pngread.c", "pngrio.c", "pngrtran.c", "pngrutil.c", "pngset.c", "pngtrans.c", "pngwio.c", "pngwrite.c", "pngwtran.c", "pngwutil.c"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_png:prepend({["CPPPATH"] = {thirdparty_dir}})
        env:prepend({["CPPPATH"] = {thirdparty_dir}})
        env_thirdparty = env_png:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        if R.truthy(R.startswith(R.index(env, "arch"), "arm")) then
            if R.truthy(env.msvc) then
                env_thirdparty:add({["CPPDEFINES"] = {{"PNG_ARM_NEON_OPT", 0}}})
            else
                env_neon = env_thirdparty:clone()
                if R.truthy((R.contains(env, "S_compiler"))) then
                    R.setindex(env_neon, "CC", R.index(env, "S_compiler"))
                end
                neon_sources = {}
                R.append(neon_sources, env_neon:object(R.add(thirdparty_dir, "arm/arm_init.c")))
                R.append(neon_sources, env_neon:object(R.add(thirdparty_dir, "arm/filter_neon_intrinsics.c")))
                R.append(neon_sources, env_neon:object(R.add(thirdparty_dir, "arm/palette_neon_intrinsics.c")))
                thirdparty_obj = R.iadd(thirdparty_obj, neon_sources)
            end
        else
            if R.truthy(R.startswith(R.index(env, "arch"), "x86")) then
                env_thirdparty:add({["CPPDEFINES"] = {"PNG_INTEL_SSE"}})
                env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "intel/intel_init.c"))
                env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "intel/filter_sse2_intrinsics.c"))
            else
                if R.truthy(((R.index(env, "arch") == "loongarch64"))) then
                    env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "loongarch/loongarch_lsx_init.c"))
                    env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "loongarch/filter_lsx_intrinsics.c"))
                else
                    if R.truthy(((R.index(env, "arch") == "ppc64"))) then
                        env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "powerpc/powerpc_init.c"))
                        env_thirdparty:sources(thirdparty_obj, R.add(thirdparty_dir, "powerpc/filter_vsx_intrinsics.c"))
                    end
                end
            end
        end
        env.drivers_sources = R.iadd(env.drivers_sources, thirdparty_obj)
    end
    driver_obj = {}
    env_png:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
