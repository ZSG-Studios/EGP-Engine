-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local directory, env, env_box2d, env_modules, env_thirdparty, key, module_obj, thirdparty_obj
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_box2d = env_modules:clone()
    env_box2d:prepend({["CPPPATH"] = {"#thirdparty/box2d/include", "#thirdparty/box2d/src", "#thirdparty/box2d/src/local_replay"}})
    for _, __item1 in ipairs(R.iter({"CCFLAGS", "CFLAGS", "CXXFLAGS"})) do
        key = __item1
        R.setindex(env_box2d, key, (function() local __item2 = {}; for _, __item3 in ipairs(R.iter(R.index(env_box2d, key))) do; local flag = __item3; if R.truthy(not R.truthy(R.startswith(R.str(flag), {"/fp:", "-ffast-math", "-Ofast", "-ffp-contract=", "-funsafe-math-optimizations"}))) then; table.insert(__item2, flag); end; end; return __item2 end)())
    end
    if R.truthy(env.msvc) then
        if R.truthy(R.index(env, "use_llvm")) then
            env_box2d:add({["CCFLAGS"] = {"/clang:-fno-fast-math", "/clang:-ffp-contract=off"}})
        else
            env_box2d:add({["CCFLAGS"] = {"/fp:precise"}})
        end
    else
        env_box2d:add({["CCFLAGS"] = {"-fno-fast-math", "-ffp-contract=off"}})
        if R.truthy((not R.contains({"macos", "ios", "windows", "web"}, R.index(env, "platform")))) then
            env:add_unique({["LIBS"] = {"m"}})
        end
    end
    env_thirdparty = env_box2d:clone()
    env_thirdparty:disable_warnings()
    thirdparty_obj = {}
    env_thirdparty:sources(thirdparty_obj, "#thirdparty/box2d/src/*.c")
    env_thirdparty:sources(thirdparty_obj, "#thirdparty/box2d/src/local_replay/*.c")
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    -- Portable restore: native bridges (C), the C++23 checkpoint library and the
    -- EGP_BOX2D_PORTABLE participant. The library links against the Superpos
    -- core, so it is selected only together with the superpos module.
    if env.module_list and env.module_list.superpos then
        local portable_c = {}
        env_thirdparty:sources(portable_c, "#thirdparty/box2d/src/portable/*.c")
        env.modules_sources = R.iadd(env.modules_sources, portable_c)
        local env_portable = env_box2d:clone()
        env_portable:disable_warnings()
        for _, flags in ipairs({"CCFLAGS", "CXXFLAGS"}) do
            R.setindex(env_portable, flags, (function() local kept = {}; for _, flag in ipairs(R.iter(R.index(env_portable, flags))) do; if not R.truthy(R.startswith(R.str(flag), {"/std:c++", "-std=c++", "-std=gnu++"})) then table.insert(kept, flag) end; end; return kept end)())
        end
        env_portable:add({["CXXFLAGS"] = R.truthy(env.msvc) and {"/std:c++latest"} or {"-std=c++23"}})
        env_portable:prepend({["CPPPATH"] = {"#thirdparty/box2d/src/portable", "#thirdparty/box2d/src/portable/core/include", "#modules/superpos/core/include"}})
        local portable_obj = {}
        env_portable:sources(portable_obj, "#thirdparty/box2d/src/portable/*.cpp")
        env_portable:sources(portable_obj, "#thirdparty/box2d/src/portable/core/src/*.cpp")
        env.modules_sources = R.iadd(env.modules_sources, portable_obj)
        env:depends(portable_obj, portable_c)
        env_box2d:add({["CPPDEFINES"] = {"EGP_BOX2D_PORTABLE"}})
    end
    module_obj = {}
    for _, __item4 in ipairs(R.iter({"", "bodies/", "joints/", "shapes/", "spaces/"})) do
        directory = __item4
        env_box2d:sources(module_obj, R.add(directory, "*.cpp"))
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
