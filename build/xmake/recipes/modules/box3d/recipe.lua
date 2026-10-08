-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local directory, env, env_box3d, env_modules, env_thirdparty, key, module_obj, thirdparty_obj
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_box3d = env_modules:clone()
    env_box3d:prepend({["CPPPATH"] = {"#thirdparty/box3d/include"}})
    env_box3d:add({["CPPDEFINES"] = {"BOX3D_DISABLE_AVX2"}})
    for _, __item1 in ipairs(R.iter({"CCFLAGS", "CFLAGS", "CXXFLAGS"})) do
        key = __item1
        R.setindex(env_box3d, key, (function() local __item2 = {}; for _, __item3 in ipairs(R.iter(R.index(env_box3d, key))) do; local flag = __item3; if R.truthy(not R.truthy(R.startswith(R.str(flag), {"/fp:", "-ffast-math", "-Ofast", "-ffp-contract=", "-funsafe-math-optimizations"}))) then; table.insert(__item2, flag); end; end; return __item2 end)())
    end
    if R.truthy(env.msvc) then
        if R.truthy(R.index(env, "use_llvm")) then
            env_box3d:add({["CCFLAGS"] = {"/clang:-fno-fast-math", "/clang:-ffp-contract=off"}})
        else
            env_box3d:add({["CCFLAGS"] = {"/fp:precise"}})
        end
    else
        env_box3d:add({["CCFLAGS"] = {"-fno-fast-math", "-ffp-contract=off"}})
        if R.truthy((not R.contains({"macos", "ios", "windows", "web"}, R.index(env, "platform")))) then
            env:add_unique({["LIBS"] = {"m"}})
        end
    end
    env_thirdparty = env_box3d:clone()
    env_thirdparty:disable_warnings()
    thirdparty_obj = {}
    env_thirdparty:sources(thirdparty_obj, "#thirdparty/box3d/src/*.c")
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_box3d:prepend({["CPPPATH"] = {"#modules/box3d/scene_backend"}})
    env_box3d:sources(module_obj, "*.cpp")
    for _, __item4 in ipairs(R.iter({"", "joints/", "misc/", "objects/", "servers/", "shapes/", "spaces/"})) do
        directory = __item4
        env_box3d:sources(module_obj, R.add(R.add("scene_backend/", directory), "*.cpp"))
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
