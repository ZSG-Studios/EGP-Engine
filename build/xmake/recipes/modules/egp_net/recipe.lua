-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, key, net_env, source, vendor_env
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    net_env = env_modules:clone()
    net_env:prepend({["CPPPATH"] = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter({"", "include", "sodium", "tlsf", "netcode", "reliable", "serialize"})) do; local path = __item2; table.insert(__item1, R.add("#thirdparty/yojimbo/", path)); end; return __item1 end)()})
    for _, __item3 in ipairs(R.iter({"CCFLAGS", "CFLAGS", "CXXFLAGS"})) do
        key = __item3
        R.setindex(net_env, key, (function() local __item4 = {}; for _, __item5 in ipairs(R.iter(R.index(net_env, key))) do; local flag = __item5; if R.truthy(not R.truthy(R.startswith(R.str(flag), {"/fp:", "-ffast-math", "-Ofast", "-ffp-contract=", "-funsafe-math-optimizations"}))) then; table.insert(__item4, flag); end; end; return __item4 end)())
    end
    if R.truthy(env.msvc) then
        if R.truthy(R.index(env, "use_llvm")) then
            net_env:add({["CCFLAGS"] = {"/clang:-fno-fast-math", "/clang:-ffp-contract=off"}})
        else
            net_env:add({["CCFLAGS"] = {"/fp:precise"}})
        end
    else
        net_env:add({["CCFLAGS"] = {"-fno-fast-math", "-ffp-contract=off"}})
    end
    if R.truthy(((R.index(env, "platform") == "windows"))) then
        env:add_unique({["LINKFLAGS"] = (function() if R.truthy(env.msvc) then return {"ws2_32.lib", "iphlpapi.lib"} else return {"-lws2_32", "-liphlpapi"} end end)()})
    end
    vendor_env = net_env:clone()
    vendor_env:disable_warnings()
    vendor_env:sources(env.modules_sources, "#thirdparty/yojimbo/source/*.cpp")
    for _, __item6 in ipairs(R.iter({"tlsf/tlsf.c", "sodium/sodium.c", "netcode/netcode.c", "reliable/reliable.c"})) do
        source = __item6
        vendor_env:sources(env.modules_sources, R.add("#thirdparty/yojimbo/", source))
    end
    net_env:sources(env.modules_sources, "*.cpp")
end
