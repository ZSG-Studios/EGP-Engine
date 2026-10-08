-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local Path, cmd, custom_build_steps, driver_obj, env, env_d3d12_rdd, env_thirdparty, extra_defines, gen_filename, in_dir, match, mesa_absdir, mesa_blacklist_inc_paths, mesa_dir, mesa_gen_absdir, mesa_gen_dir, mesa_gen_include_paths, mesa_libs, mesa_private_inc_paths, mesa_ver, methods, os, out_file_full_path, out_full_path, print_error, re, req_version_major, req_version_minor, subdir, sys, thirdparty_obj, v
    os = R.os
    re = R.re
    sys = R.sys
    Path = R.Path
    methods = graph:methods()
    print_error = graph:methods().print_error
    req_version_major = 25
    req_version_minor = 3
    env = graph:use("env")
    env_d3d12_rdd = env:clone()
    if R.truthy((function() local v = ((R.index(env, "agility_sdk_path") ~= "")); if not R.truthy(v) then return v end; return os.path.exists(R.index(env, "agility_sdk_path")) end)()) then
        env_d3d12_rdd:add({["CPPDEFINES"] = {"AGILITY_SDK_ENABLED"}})
        if R.truthy(R.index(env, "agility_sdk_multiarch")) then
            env_d3d12_rdd:add({["CPPDEFINES"] = {"AGILITY_SDK_MULTIARCH_ENABLED"}})
        end
    end
    if R.truthy(R.index(env, "use_pix")) then
        env_d3d12_rdd:add({["CPPDEFINES"] = {"PIX_ENABLED"}})
        env_d3d12_rdd:add({["CPPPATH"] = {R.add(R.index(env, "pix_path"), "/Include")}})
    end
    if R.truthy((R.contains(env:get("supported", {}), "dcomp"))) then
        env_d3d12_rdd:add({["CPPDEFINES"] = {"DCOMP_ENABLED"}})
        env:add({["CPPDEFINES"] = {"DCOMP_ENABLED"}})
    end
    mesa_libs = import("build.xmake.sdk_paths", {rootdir=graph.root}).resolve(env.options, graph.root).mesa_libs
    mesa_dir = R.replace(R.add(mesa_libs, "/godot-mesa"), "\\", "/")
    mesa_gen_dir = R.replace(R.add(mesa_libs, "/godot-mesa/generated"), "\\", "/")
    mesa_absdir = graph:directory(mesa_dir).abspath
    mesa_gen_absdir = graph:directory(R.add(mesa_dir, "/generated")).abspath
    -- The pinned NIR SDK includes these generated headers; never invoke Mesa generators.
    mesa_gen_include_paths = {mesa_gen_dir .. "/src"}
    for _, directory in ipairs({"src/compiler", "src/compiler/nir", "src/compiler/spirv", "src/util/format"}) do
        table.insert(mesa_gen_include_paths, mesa_gen_dir .. "/" .. directory)
    end
    mesa_private_inc_paths = (function() local __item2 = {}; for _, __item3 in ipairs(R.iter(os.walk(mesa_absdir))) do; local v = __item3; table.insert(__item2, R.index(v, 0)); end; return __item2 end)()
    mesa_private_inc_paths = (function() local __item4 = {}; for _, __item5 in ipairs(R.iter(mesa_private_inc_paths)) do; local v = __item5; table.insert(__item4, R.replace(v, mesa_absdir, mesa_dir)); end; return __item4 end)()
    mesa_private_inc_paths = (function() local __item6 = {}; for _, __item7 in ipairs(R.iter(mesa_private_inc_paths)) do; local v = __item7; table.insert(__item6, R.replace(v, "\\", "/")); end; return __item6 end)()
    mesa_private_inc_paths = (function() local __item8 = {}; for _, __item9 in ipairs(R.iter(mesa_private_inc_paths)) do; local v = __item9; if R.truthy(not R.truthy(R.startswith(v, mesa_gen_dir))) then; table.insert(__item8, v); end; end; return __item8 end)()
    R.sort(mesa_private_inc_paths)
    mesa_private_inc_paths = R.iadd(mesa_private_inc_paths, mesa_gen_include_paths)
    mesa_blacklist_inc_paths = {"src/c11"}
    mesa_blacklist_inc_paths = (function() local __item10 = {}; for _, __item11 in ipairs(R.iter(mesa_blacklist_inc_paths)) do; local v = __item11; table.insert(__item10, R.add(R.add(mesa_dir, "/"), v)); end; return __item10 end)()
    mesa_private_inc_paths = (function() local __item12 = {}; for _, __item13 in ipairs(R.iter(mesa_private_inc_paths)) do; local v = __item13; if R.truthy((not R.contains(mesa_blacklist_inc_paths, v))) then; table.insert(__item12, v); end; end; return __item12 end)()
    extra_defines = {"WINDOWS_NO_FUTEX"}
    mesa_ver = R.Path(R.add(mesa_absdir, "/VERSION.info"))
    if R.truthy(not R.truthy(mesa_ver:is_file())) then
        mesa_ver = R.Path(R.add(mesa_absdir, "/VERSION"))
    end
    match = re.match("([0-9]*).([0-9]*).([0-9]*)-?([0-9.+]*)", R.strip(mesa_ver:read_text()))
    if R.truthy((function() local v = ((match == nil)); if R.truthy(v) then return v end; local v = ((R.int(match:group(1)) ~= req_version_major)); if R.truthy(v) then return v end; return (function() local v = ((R.int(match:group(1)) == req_version_major)); if not R.truthy(v) then return v end; return ((R.int(match:group(2)) < req_version_minor)) end)() end)()) then
        graph:message("print_error", "Direct3D 12 SDK dependencies are missing or outdated. Run xmake lua misc/scripts/install_build_dependencies.lua d3d12.")
        sys.exit(255)
    end
    extra_defines = R.iadd(extra_defines, {"__STDC_CONSTANT_MACROS", "__STDC_FORMAT_MACROS", "__STDC_LIMIT_MACROS", {"PACKAGE_VERSION", R.add(R.add('"', R.strip(mesa_ver:read_text())), '"')}, {"PACKAGE_BUGREPORT", '"https://gitlab.freedesktop.org/mesa/mesa/-/issues"'}, "PIPE_SUBSYSTEM_WINDOWS_USER", {"_Static_assert", "static_assert"}, "HAVE_STRUCT_TIMESPEC"})
    if R.truthy(env.msvc) then
        extra_defines = R.iadd(extra_defines, {"_USE_MATH_DEFINES", "VC_EXTRALEAN", "_CRT_SECURE_NO_WARNINGS", "_CRT_SECURE_NO_DEPRECATE", "_SCL_SECURE_NO_WARNINGS", "_SCL_SECURE_NO_DEPRECATE", "_ALLOW_KEYWORD_MACROS", "NOMINMAX"})
    else
        extra_defines = R.iadd(extra_defines, {{"__REQUIRED_RPCNDR_H_VERSION__", 475}})
        if R.truthy((function() local v = methods.using_gcc(env); if not R.truthy(v) then return v end; return ((R.index(methods.get_compiler_version(env), "major") < 13)) end)()) then
            env_d3d12_rdd:add({["CCFLAGS"] = {"-Wno-unknown-pragmas"}})
        end
    end
    env_d3d12_rdd:prepend({["CPPPATH"] = mesa_private_inc_paths})
    env_d3d12_rdd:add({["CPPDEFINES"] = extra_defines})
    thirdparty_obj = {}
    env_thirdparty = env_d3d12_rdd:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:prepend({["CPPPATH"] = {"#thirdparty/directx_headers/include/directx", "#thirdparty/directx_headers/include/dxguids", "#thirdparty/d3d12ma"}})
    env_thirdparty:sources(thirdparty_obj, "#thirdparty/d3d12ma/D3D12MemAlloc.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, thirdparty_obj)
    driver_obj = {}
    env_d3d12_rdd:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
