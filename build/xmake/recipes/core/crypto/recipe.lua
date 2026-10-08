-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local _escape_define, configs, core_obj, env, env_crypto, env_thirdparty, has_module, is_builtin, platform_obj, psa_dir, thirdparty_mbedtls_dir, thirdparty_mbedtls_sources, thirdparty_obj
    env = graph:use("env")
    _escape_define = function(env, config)
        return "\"" .. config .. "\""
    end
    env_crypto = env:clone()
    env_crypto:bind_method(_escape_define, "EscapeDefine")
    is_builtin = R.index(env, "builtin_mbedtls")
    has_module = R.index(env, "module_mbedtls_enabled")
    thirdparty_obj = {}
    configs = {}
    if R.truthy((function() local v = is_builtin; if R.truthy(v) then return v end; return not R.truthy(has_module) end)()) then
        env_crypto:prepend({["CPPPATH"] = {"#thirdparty/mbedtls/godot/", "#thirdparty/mbedtls/include/", "#thirdparty/mbedtls/tf-psa-crypto/include/", "#thirdparty/mbedtls/tf-psa-crypto/drivers/builtin/include/"}})
        env_crypto:add({["CPPDEFINES"] = {{"TF_PSA_CRYPTO_CONFIG_FILE", env_crypto:EscapeDefine("godot_psa_config.h")}, {"MBEDTLS_CONFIG_FILE", env_crypto:EscapeDefine("godot_mbedtls_config.h")}}})
        configs = {"#thirdparty/mbedtls/godot/godot_psa_config.h", "#thirdparty/mbedtls/godot/godot_mbedtls_config.h"}
        if R.truthy(not R.truthy(has_module)) then
            env_crypto:add({["CPPDEFINES"] = {"GODOT_MBEDTLS_LIGHT"}})
        end
        platform_obj = {}
        env_crypto:sources(platform_obj, {"#thirdparty/mbedtls/godot/godot_mbedtls_platform.cpp"})
        env.core_sources = R.iadd(env.core_sources, platform_obj)
        env:depends(platform_obj, configs)
    end
    if R.truthy(not R.truthy(has_module)) then
        env_thirdparty = env_crypto:clone()
        env_thirdparty:disable_warnings()
        psa_dir = "#thirdparty/mbedtls/tf-psa-crypto/"
        env_thirdparty:prepend({["CPPPATH"] = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter({"include/", "core/", "dispatch/", "extras", "platform/", "utilities/", "drivers/builtin/src/"})) do; local d = __item2; table.insert(__item1, R.add(psa_dir, d)); end; return __item1 end)()})
        env_thirdparty:prepend({["CPPPATH"] = {"#thirdparty/mbedtls/tf-psa-crypto/drivers/builtin/include/"}})
        thirdparty_mbedtls_dir = "#thirdparty/mbedtls/"
        thirdparty_mbedtls_sources = {"tf-psa-crypto/core/psa_crypto.c", "tf-psa-crypto/core/psa_crypto_client.c", "tf-psa-crypto/core/psa_crypto_driver_wrappers_no_static.c", "tf-psa-crypto/core/psa_crypto_random.c", "tf-psa-crypto/core/psa_crypto_slot_management.c", "tf-psa-crypto/utilities/base64.c", "tf-psa-crypto/utilities/constant_time.c", "tf-psa-crypto/platform/threading.c", "tf-psa-crypto/extras/md.c", "tf-psa-crypto/drivers/builtin/src/aes.c", "tf-psa-crypto/drivers/builtin/src/ctr_drbg.c", "tf-psa-crypto/drivers/builtin/src/cipher.c", "tf-psa-crypto/drivers/builtin/src/cipher_wrap.c", "tf-psa-crypto/drivers/builtin/src/entropy.c", "tf-psa-crypto/drivers/builtin/src/entropy_poll.c", "tf-psa-crypto/drivers/builtin/src/md5.c", "tf-psa-crypto/drivers/builtin/src/psa_crypto_cipher.c", "tf-psa-crypto/drivers/builtin/src/psa_crypto_hash.c", "tf-psa-crypto/drivers/builtin/src/psa_crypto_rsa.c", "tf-psa-crypto/drivers/builtin/src/psa_util_internal.c", "tf-psa-crypto/drivers/builtin/src/sha1.c", "tf-psa-crypto/drivers/builtin/src/sha256.c"}
        thirdparty_mbedtls_sources = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(thirdparty_mbedtls_sources)) do; local file = __item4; table.insert(__item3, R.add(thirdparty_mbedtls_dir, file)); end; return __item3 end)()
        env_thirdparty:sources(thirdparty_obj, thirdparty_mbedtls_sources)
        env_thirdparty:depends(thirdparty_obj, configs)
        env.core_sources = R.iadd(env.core_sources, thirdparty_obj)
    end
    core_obj = {}
    env_crypto:sources(core_obj, "*.cpp")
    env.core_sources = R.iadd(env.core_sources, core_obj)
    env:depends(core_obj, R.add(thirdparty_obj, configs))
end
