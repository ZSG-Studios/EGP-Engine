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
    -- Private adapter sources include the module's public headers by name; the module root
    -- comes last so it can never shadow an engine header (clean builds failed with C1083).
    superpos_env:add({["CPPPATH"] = {"#modules/superpos"}})
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
    -- Opt-in embedded durable recovery (plan: migration is opt-in). Services
    -- and the pinned SQLite come from the module-owned core mirror; crypto
    -- borrows EGP's builtin Mbed TLS/PSA through the DTLS profile.
    if R.truthy(R.index(env, "superpos_durable_recovery")) then
        if not R.truthy(R.index(env, "superpos_dtls")) then
            raise("Superpos durable recovery requires superpos_dtls (borrowed EGP PSA digest and nonce)")
        end
        local recovery = assert(adapter_manifest.features and adapter_manifest.features.durable_recovery,
            "Missing module-owned durable recovery contract")
        assert(recovery.version == 1 and recovery.core_feature == "services", "Unsupported durable recovery contract")
        local services = assert(R.index(R.index(manifest, "features"), "services"), "Core mirror lacks the services feature")
        superpos_env:prepend({["CPPPATH"] = {"#modules/superpos/core/services/include", "#modules/superpos/core/services/src/control/include",
            "#modules/superpos/core/services/src", "#modules/superpos/" .. recovery.sqlite.directory}})
        superpos_env:add({["CPPDEFINES"] = {"SUPERPOS_HAS_DURABLE_RECOVERY"}})
        for _, service in ipairs(services.sources) do
            assert(service:startswith("services/src/") and service:endswith(".cpp") and not service:find("..", 1, true),
                "Invalid Superpos services manifest entry")
            superpos_env:sources(env.modules_sources, "core/" .. service)
        end
        for _, recovery_source in ipairs(recovery.sources or {}) do
            assert(recovery_source:startswith("private/recovery/") and recovery_source:endswith(".cpp") and not recovery_source:find("..", 1, true),
                "Invalid durable recovery module source")
            superpos_env:sources(env.modules_sources, recovery_source)
        end
        local pin = json.loads(superpos_env:file(recovery.sqlite.manifest):read())
        assert(pin.version == recovery.sqlite.version, "SQLite pin differs from the durable recovery contract")
        local sqlite_env = superpos_env:clone()
        sqlite_env:add({["CPPDEFINES"] = pin.build_definitions})
        sqlite_env:disable_warnings()
        sqlite_env:sources(env.modules_sources, recovery.sqlite.directory .. "/sqlite3.c")
    end
    for _, __item4 in ipairs(R.iter(core_sources)) do
        source = __item4
        if R.truthy((function() local v = not R.truthy(R.startswith(source, "src/")); if R.truthy(v) then return v end; local v = (R.contains(source, "..")); if R.truthy(v) then return v end; return not R.truthy(R.endswith(source, ".cpp")) end)()) then
            raise("Invalid Superpos source manifest entry")
        end
        superpos_env:sources(env.modules_sources, R.add("core/", source))
    end
end
