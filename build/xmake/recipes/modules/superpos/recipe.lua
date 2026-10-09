-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local _escape_define, core_sources, env, env_modules, json, key, manifest, source, superpos_env
    json = R.json
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    _escape_define = function(env, config)
        return "\"" .. config .. "\""
    end
    superpos_env = env_modules:clone()
    superpos_env:bind_method(_escape_define, "EscapeDefine")
    for _, __item1 in ipairs(R.iter({"CCFLAGS", "CXXFLAGS"})) do
        key = __item1
        R.setindex(superpos_env, key, (function() local __item2 = {}; for _, __item3 in ipairs(R.iter(R.index(superpos_env, key))) do; local flag = __item3; if R.truthy(not R.truthy(R.startswith(R.str(flag), {"/std:c++", "-std=c++", "-std=gnu++", "/fp:", "-ffast-math", "-Ofast", "-ffp-contract=", "-funsafe-math-optimizations"}))) then; table.insert(__item2, flag); end; end; return __item2 end)())
    end
    if R.truthy(env.msvc) then
        superpos_env:add({["CXXFLAGS"] = {"/std:c++latest"}})
        superpos_env:add({["CCFLAGS"] = {"/fp:strict"}})
    else
        superpos_env:add({["CXXFLAGS"] = {"-std=c++23"}})
        superpos_env:add({["CCFLAGS"] = {"-fno-fast-math", "-ffp-contract=off"}})
    end
    superpos_env:prepend({["CPPPATH"] = {"#modules/superpos/core/include"}})
    if R.truthy((function() local v = R.index(env, "superpos_dtls"); if not R.truthy(v) then return v end; return not R.truthy(env:get("builtin_mbedtls", false)) end)()) then
        raise("Superpos-EGP initially qualifies EGP's builtin MbedTLS profile only")
    end
    if R.truthy(R.index(env, "superpos_dtls")) then
        superpos_env:prepend({["CPPPATH"] = {"#thirdparty/mbedtls/godot", "#thirdparty/mbedtls/include", "#thirdparty/mbedtls/tf-psa-crypto/include", "#thirdparty/mbedtls/tf-psa-crypto/drivers/builtin/include"}})
    end
    if R.truthy(R.index(env, "superpos_dtls")) then
        superpos_env:add({["CPPDEFINES"] = {{"MBEDTLS_CONFIG_FILE", superpos_env:EscapeDefine("godot_mbedtls_config.h")}, {"TF_PSA_CRYPTO_CONFIG_FILE", superpos_env:EscapeDefine("godot_psa_config.h")}, "SUPERPOS_HAS_DTLS"}})
    end
    if R.truthy((function() local v = ((R.index(env, "platform") == "windows")); if not R.truthy(v) then return v end; return R.index(env, "superpos_dtls") end)()) then
        env:add_unique({["LINKFLAGS"] = (function() if R.truthy(env.msvc) then return {"ws2_32.lib", "bcrypt.lib"} else return {"-lws2_32", "-lbcrypt"} end end)()})
    end
    -- Select the adapter's complete manifest, including private memory backing.
    -- A root glob silently omitted private/module_memory.cpp and failed linking.
    local adapter_manifest = json.loads(superpos_env:file("source_manifest.json"):read())
    -- Only the selected private Box2D participant needs this native header root.
    -- Public SDK headers and other module environments retain their includes.
    if adapter_manifest.experimental_local_replay and env.module_list.box2d then
        assert(adapter_manifest.experimental_local_replay.native_include == "#thirdparty/box2d/include",
            "Unsupported Superpos local replay native header profile")
        superpos_env:prepend({["CPPPATH"] = {"#thirdparty/box2d/include"}})
    end
    for _, adapter_source in ipairs(adapter_manifest.sources) do
        assert(not adapter_source:find("..", 1, true) and adapter_source:endswith(".cpp"),
            "Invalid Superpos adapter source manifest entry")
        superpos_env:sources(env.modules_sources, adapter_source)
    end
    manifest = json.loads(superpos_env:file("core/source_manifest.json"):read())
    if R.truthy((function() local v = ((R.get(manifest, "version") ~= 1)); if R.truthy(v) then return v end; return ((R.get(manifest, "language") ~= "c++23")) end)()) then
        raise("Unsupported staged Superpos source manifest")
    end
    core_sources = R.list(R.index(manifest, "core"))
    if R.truthy(R.index(env, "superpos_dtls")) then
        R.extend(core_sources, R.index(R.index(R.index(manifest, "features"), "dtls"), "sources"))
    end
    for _, __item4 in ipairs(R.iter(core_sources)) do
        source = __item4
        if R.truthy((function() local v = not R.truthy(R.startswith(source, "src/")); if R.truthy(v) then return v end; local v = (R.contains(source, "..")); if R.truthy(v) then return v end; return not R.truthy(R.endswith(source, ".cpp")) end)()) then
            raise("Invalid Superpos source manifest entry")
        end
        superpos_env:sources(env.modules_sources, R.add("core/", source))
    end
end
