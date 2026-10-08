-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local embree_src, env, env_modules, env_raycast, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_raycast = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_embree")) then
        thirdparty_dir = "#thirdparty/embree/"
        embree_src = {"common/sys/sysinfo.cpp", "common/sys/alloc.cpp", "common/sys/estring.cpp", "common/sys/filename.cpp", "common/sys/library.cpp", "common/sys/thread.cpp", "common/sys/regression.cpp", "common/sys/mutex.cpp", "common/sys/condition.cpp", "common/sys/barrier.cpp", "common/math/constants.cpp", "common/simd/sse.cpp", "common/lexers/stringstream.cpp", "common/lexers/tokenstream.cpp", "common/tasking/taskschedulerinternal.cpp", "kernels/common/device.cpp", "kernels/common/stat.cpp", "kernels/common/acceln.cpp", "kernels/common/accelset.cpp", "kernels/common/state.cpp", "kernels/common/rtcore.cpp", "kernels/common/rtcore_builder.cpp", "kernels/common/scene.cpp", "kernels/common/scene_verify.cpp", "kernels/common/alloc.cpp", "kernels/common/geometry.cpp", "kernels/common/scene_triangle_mesh.cpp", "kernels/geometry/primitive4.cpp", "kernels/builders/primrefgen.cpp", "kernels/bvh/bvh.cpp", "kernels/bvh/bvh_statistics.cpp", "kernels/bvh/bvh4_factory.cpp", "kernels/bvh/bvh8_factory.cpp", "kernels/bvh/bvh_collider.cpp", "kernels/bvh/bvh_rotate.cpp", "kernels/bvh/bvh_refit.cpp", "kernels/bvh/bvh_builder.cpp", "kernels/bvh/bvh_builder_morton.cpp", "kernels/bvh/bvh_builder_sah.cpp", "kernels/bvh/bvh_builder_sah_spatial.cpp", "kernels/bvh/bvh_builder_sah_mb.cpp", "kernels/bvh/bvh_builder_twolevel.cpp", "kernels/bvh/bvh_intersector1_bvh4.cpp", "kernels/bvh/bvh_intersector_hybrid4_bvh4.cpp"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(embree_src)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_raycast:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "include")}})
        env_raycast:add({["CPPDEFINES"] = {"EMBREE_TARGET_SSE2", "EMBREE_LOWEST_ISA", "TASKING_INTERNAL"}})
        env_raycast:add_unique({["CPPDEFINES"] = {"NDEBUG"}})
        if R.truthy(((R.index(env, "platform") == "windows"))) then
            if R.truthy(env.msvc) then
                env:add({["LINKFLAGS"] = {"psapi.lib"}})
            else
                env:add({["LIBS"] = {"psapi"}})
            end
        end
        if R.truthy(env.msvc) then
            env_raycast:add({["CCFLAGS"] = {"/wd4324"}})
        end
        env_thirdparty = env_raycast:clone()
        env_thirdparty:force_optimization_on_debug()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        if R.truthy((function() local v = ((R.index(env, "arch") ~= "x86_64")); if R.truthy(v) then return v end; return env.msvc end)()) then
            env_thirdparty:add({["CPPDEFINES"] = {"__SSE__", "__SSE2__"}})
        end
        if R.truthy((function() local v = ((R.index(env, "arch") == "x86_64")); if not R.truthy(v) then return v end; return env.msvc end)()) then
            env_thirdparty:add({["CPPDEFINES"] = {"__SSE3__", "__SSSE3__", "__SSE4_1__", "__SSE4_2__"}})
        end
        if R.truthy(((R.index(env, "platform") == "web"))) then
            env_thirdparty:add({["CXXFLAGS"] = {"-msimd128"}})
        end
        if R.truthy(not R.truthy(env.msvc)) then
            if R.truthy((function() local v = ((R.index(env, "arch") == "arm64")); if not R.truthy(v) then return v end; local v = ((R.index(env, "platform") == "linuxbsd")); if not R.truthy(v) then return v end; return not R.truthy(R.index(env, "use_llvm")) end)()) then
                env_thirdparty:add({["CXXFLAGS"] = {"-flax-vector-conversions"}})
            end
            env_thirdparty:add({["CXXFLAGS"] = {"-fno-strict-overflow", "-fno-delete-null-pointer-checks", "-fwrapv", "-fsigned-char", "-fno-strict-aliasing", "-fno-tree-vectorize", "-fvisibility=hidden", "-fvisibility-inlines-hidden"}})
        end
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_raycast:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
