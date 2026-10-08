-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local _escape_define, configs, env, env_library, env_mbed_tls, env_modules, env_psa, mbedtls_dir, mbedtls_sources, module_obj, psa_dir, psa_obj, psa_sources, thirdparty_obj
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    _escape_define = function(env, config)
        return "\"" .. config .. "\""
    end
    env_mbed_tls = env_modules:clone()
    env_mbed_tls:bind_method(_escape_define, "EscapeDefine")
    thirdparty_obj = {}
    configs = {}
    if R.truthy(R.index(env, "builtin_mbedtls")) then
        env_mbed_tls:add({["CPPDEFINES"] = {{"TF_PSA_CRYPTO_CONFIG_FILE", env_mbed_tls:EscapeDefine("godot_psa_config.h")}, {"MBEDTLS_CONFIG_FILE", env_mbed_tls:EscapeDefine("godot_mbedtls_config.h")}}})
        configs = {"#thirdparty/mbedtls/godot/godot_psa_config.h", "#thirdparty/mbedtls/godot/godot_mbedtls_config.h"}
        env_mbed_tls:prepend({["CPPPATH"] = {"#thirdparty/mbedtls/godot/", "#thirdparty/mbedtls/include/", "#thirdparty/mbedtls/tf-psa-crypto/include/", "#thirdparty/mbedtls/tf-psa-crypto/drivers/builtin/include/"}})
        env_psa = env_mbed_tls:clone()
        psa_obj = {}
        psa_sources = {"drivers/builtin/src/aes.c", "drivers/builtin/src/aesce.c", "drivers/builtin/src/aesni.c", "drivers/builtin/src/aria.c", "drivers/builtin/src/bignum.c", "drivers/builtin/src/bignum_core.c", "drivers/builtin/src/bignum_mod.c", "drivers/builtin/src/bignum_mod_raw.c", "drivers/builtin/src/block_cipher.c", "drivers/builtin/src/camellia.c", "drivers/builtin/src/ccm.c", "drivers/builtin/src/chacha20.c", "drivers/builtin/src/chacha20_neon.c", "drivers/builtin/src/chachapoly.c", "drivers/builtin/src/cipher.c", "drivers/builtin/src/cipher_wrap.c", "drivers/builtin/src/cmac.c", "drivers/builtin/src/ctr_drbg.c", "drivers/builtin/src/ecdsa.c", "drivers/builtin/src/ecjpake.c", "drivers/builtin/src/ecp.c", "drivers/builtin/src/ecp_curves.c", "drivers/builtin/src/ecp_curves_new.c", "drivers/builtin/src/entropy.c", "drivers/builtin/src/entropy_poll.c", "drivers/builtin/src/gcm.c", "drivers/builtin/src/hmac_drbg.c", "drivers/builtin/src/md5.c", "drivers/builtin/src/poly1305.c", "drivers/builtin/src/psa_crypto_aead.c", "drivers/builtin/src/psa_crypto_cipher.c", "drivers/builtin/src/psa_crypto_ecp.c", "drivers/builtin/src/psa_crypto_ffdh.c", "drivers/builtin/src/psa_crypto_hash.c", "drivers/builtin/src/psa_crypto_mac.c", "drivers/builtin/src/psa_crypto_pake.c", "drivers/builtin/src/psa_crypto_rsa.c", "drivers/builtin/src/psa_crypto_xof.c", "drivers/builtin/src/psa_util_internal.c", "drivers/builtin/src/ripemd160.c", "drivers/builtin/src/rsa.c", "drivers/builtin/src/rsa_alt_helpers.c", "drivers/builtin/src/sha1.c", "drivers/builtin/src/sha256.c", "drivers/builtin/src/sha3.c", "drivers/builtin/src/sha512.c", "extras/lmots.c", "extras/lms.c", "extras/md.c", "extras/nist_kw.c", "extras/pk.c", "extras/pk_ecc.c", "extras/pk_rsa.c", "extras/pk_wrap.c", "extras/pkparse.c", "extras/pkwrite.c", "platform/memory_buffer_alloc.c", "platform/platform.c", "platform/platform_util.c", "platform/threading.c", "utilities/asn1parse.c", "utilities/asn1write.c", "utilities/base64.c", "utilities/constant_time.c", "utilities/oid.c", "utilities/pem.c", "utilities/pkcs5.c", "core/psa_crypto.c", "core/psa_crypto_client.c", "core/psa_crypto_random.c", "core/psa_crypto_slot_management.c", "core/psa_crypto_storage.c", "core/psa_its_file.c", "core/psa_util.c", "core/tf_psa_crypto_config.c", "core/tf_psa_crypto_version.c", "core/psa_crypto_driver_wrappers_no_static.c"}
        psa_dir = "#thirdparty/mbedtls/tf-psa-crypto/"
        psa_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(psa_sources)) do; local file = __item2; table.insert(__item1, R.add(psa_dir, file)); end; return __item1 end)()
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "drivers/builtin/src/")}})
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "platform/")}})
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "utilities/")}})
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "extras/")}})
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "dispatch/")}})
        env_psa:prepend({["CPPPATH"] = {R.add(psa_dir, "core/")}})
        env_psa:disable_warnings()
        env_psa:sources(psa_obj, psa_sources)
        env_psa:depends(psa_obj, configs)
        env.modules_sources = R.iadd(env.modules_sources, psa_obj)
        mbedtls_sources = {"library/error.c", "library/mbedtls_config.c", "library/pkcs7.c", "library/x509.c", "library/x509_create.c", "library/x509_crl.c", "library/x509_crt.c", "library/x509_csr.c", "library/x509_oid.c", "library/x509write.c", "library/x509write_crt.c", "library/x509write_csr.c", "library/debug.c", "library/mps_reader.c", "library/mps_trace.c", "library/net_sockets.c", "library/ssl_cache.c", "library/ssl_ciphersuites.c", "library/ssl_client.c", "library/ssl_cookie.c", "library/ssl_debug_helpers_generated.c", "library/ssl_msg.c", "library/ssl_ticket.c", "library/ssl_tls.c", "library/ssl_tls12_client.c", "library/ssl_tls12_server.c", "library/ssl_tls13_keys.c", "library/ssl_tls13_server.c", "library/ssl_tls13_client.c", "library/ssl_tls13_generic.c", "library/timing.c", "library/version.c", "library/version_features.c"}
        mbedtls_dir = "#thirdparty/mbedtls/"
        mbedtls_sources = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(mbedtls_sources)) do; local file = __item4; table.insert(__item3, R.add(mbedtls_dir, file)); end; return __item3 end)()
        env_library = env_psa:clone()
        env_library:prepend({["CPPPATH"] = {"#thirdparty/mbedtls/library/"}})
        env_library:disable_warnings()
        env_library:sources(thirdparty_obj, mbedtls_sources)
        env_library:depends(thirdparty_obj, R.add(psa_obj, configs))
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_mbed_tls:sources(module_obj, "*.cpp")
    env_mbed_tls:sources(module_obj, "./mbedtls_cpp_compat.c")
    if R.truthy(R.index(env, "tests")) then
        env_mbed_tls:sources(module_obj, "./tests/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, configs)
    env:depends(module_obj, R.add(thirdparty_obj, configs))
end
