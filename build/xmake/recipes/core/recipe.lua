-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local core_builders, encryption_key, encryption_key_var, env, env_thirdparty, gen_encrypt, gen_hash, lib, methods, os, thirdparty_brotli_dir, thirdparty_brotli_sources, thirdparty_clipper_dir, thirdparty_clipper_sources, thirdparty_minizip_dir, thirdparty_minizip_sources, thirdparty_misc_dir, thirdparty_misc_sources, thirdparty_obj, thirdparty_pcre2_dir, thirdparty_pcre2_flags, thirdparty_pcre2_sources, thirdparty_zlib_dir, thirdparty_zlib_sources, thirdparty_zstd_dir, thirdparty_zstd_sources
    os = R.os
    core_builders = graph:builders("core_builders")
    methods = graph:methods()
    env = graph:use("env")
    env.core_sources = {}
    thirdparty_obj = {}
    env_thirdparty = env:clone()
    env_thirdparty:disable_warnings()
    thirdparty_misc_dir = "#thirdparty/misc/"
    thirdparty_misc_sources = {"fastlz.c", "r128.c", "smaz.c", "pcg.cpp", "polypartition.cpp", "smolv.cpp"}
    thirdparty_misc_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_misc_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_misc_dir, file)); end; return __item1 end)()
    env_thirdparty:sources(thirdparty_obj, thirdparty_misc_sources)
    if R.truthy((function() local v = R.index(env, "brotli"); if not R.truthy(v) then return v end; return R.index(env, "builtin_brotli") end)()) then
        thirdparty_brotli_dir = "#thirdparty/brotli/"
        thirdparty_brotli_sources = {"common/constants.c", "common/context.c", "common/dictionary.c", "common/platform.c", "common/shared_dictionary.c", "common/transform.c", "dec/bit_reader.c", "dec/decode.c", "dec/huffman.c", "dec/prefix.c", "dec/state.c", "dec/static_init.c"}
        thirdparty_brotli_sources = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(thirdparty_brotli_sources)) do; local file = __item4; table.insert(__item3, R.add(thirdparty_brotli_dir, file)); end; return __item3 end)()
        env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_brotli_dir, "include")}})
        env:prepend({["CPPPATH"] = {R.add(thirdparty_brotli_dir, "include")}})
        if R.truthy((function() local v = env:get("use_ubsan"); if R.truthy(v) then return v end; local v = env:get("use_asan"); if R.truthy(v) then return v end; local v = env:get("use_tsan"); if R.truthy(v) then return v end; local v = env:get("use_lsan"); if R.truthy(v) then return v end; return env:get("use_msan") end)()) then
            env_thirdparty:add({["CPPDEFINES"] = {"BROTLI_BUILD_PORTABLE"}})
        end
        env_thirdparty:sources(thirdparty_obj, thirdparty_brotli_sources)
    end
    if R.truthy(R.index(env, "builtin_pcre2")) then
        thirdparty_pcre2_dir = "#thirdparty/pcre2/src/"
        thirdparty_pcre2_sources = {"pcre2_auto_possess.c", "pcre2_chartables.c", "pcre2_chkdint.c", "pcre2_compile.c", "pcre2_compile_cgroup.c", "pcre2_compile_class.c", "pcre2_config.c", "pcre2_context.c", "pcre2_convert.c", "pcre2_dfa_match.c", "pcre2_error.c", "pcre2_extuni.c", "pcre2_find_bracket.c", "pcre2_jit_compile.c", "pcre2_maketables.c", "pcre2_match.c", "pcre2_match_data.c", "pcre2_match_next.c", "pcre2_newline.c", "pcre2_ord2utf.c", "pcre2_pattern_info.c", "pcre2_script_run.c", "pcre2_serialize.c", "pcre2_string_utils.c", "pcre2_study.c", "pcre2_substitute.c", "pcre2_substring.c", "pcre2_tables.c", "pcre2_ucd.c", "pcre2_valid_utf.c", "pcre2_xclass.c"}
        thirdparty_pcre2_sources = (function() local __item5 = {}; for _, __item6 in ipairs(R.iter(thirdparty_pcre2_sources)) do; local file = __item6; table.insert(__item5, R.add(thirdparty_pcre2_dir, file)); end; return __item5 end)()
        thirdparty_pcre2_flags = {"PCRE2_STATIC", "HAVE_CONFIG_H", "SUPPORT_UNICODE", {"PCRE2_CODE_UNIT_WIDTH", 32}}
        if R.truthy(R.index(env, "builtin_pcre2_with_jit")) then
            R.append(thirdparty_pcre2_flags, "SUPPORT_JIT")
        end
        env_thirdparty:add({["CPPDEFINES"] = thirdparty_pcre2_flags})
        env_thirdparty:prepend({["CPPPATH"] = {thirdparty_pcre2_dir}})
        env_thirdparty:sources(thirdparty_obj, thirdparty_pcre2_sources)
    end
    if R.truthy(R.index(env, "builtin_clipper2")) then
        thirdparty_clipper_dir = "#thirdparty/clipper2/"
        thirdparty_clipper_sources = {"src/clipper.engine.cpp", "src/clipper.offset.cpp", "src/clipper.rectclip.cpp"}
        thirdparty_clipper_sources = (function() local __item7 = {}; for _, __item8 in ipairs(R.iter(thirdparty_clipper_sources)) do; local file = __item8; table.insert(__item7, R.add(thirdparty_clipper_dir, file)); end; return __item7 end)()
        env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_clipper_dir, "include")}})
        env:prepend({["CPPPATH"] = {R.add(thirdparty_clipper_dir, "include")}})
        env_thirdparty:add({["CPPDEFINES"] = {"CLIPPER2_ENABLED"}})
        env:add({["CPPDEFINES"] = {"CLIPPER2_ENABLED"}})
        env_thirdparty:sources(thirdparty_obj, thirdparty_clipper_sources)
    end
    if R.truthy(R.index(env, "builtin_zlib")) then
        thirdparty_zlib_dir = "#thirdparty/zlib/"
        thirdparty_zlib_sources = {"adler32.c", "compress.c", "crc32.c", "deflate.c", "inffast.c", "inflate.c", "inftrees.c", "trees.c", "uncompr.c", "zutil.c"}
        thirdparty_zlib_sources = (function() local __item9 = {}; for _, __item10 in ipairs(R.iter(thirdparty_zlib_sources)) do; local file = __item10; table.insert(__item9, R.add(thirdparty_zlib_dir, file)); end; return __item9 end)()
        env_thirdparty:prepend({["CPPPATH"] = {thirdparty_zlib_dir}})
        env:prepend({["CPPPATH"] = {thirdparty_zlib_dir}})
        if R.truthy(env.dev_build) then
            env_thirdparty:add({["CPPDEFINES"] = {"ZLIB_DEBUG"}})
            env:add({["CPPDEFINES"] = {"ZLIB_DEBUG"}})
        end
        env_thirdparty:sources(thirdparty_obj, thirdparty_zlib_sources)
    end
    thirdparty_minizip_dir = "#thirdparty/minizip/"
    thirdparty_minizip_sources = {"ioapi.c", "unzip.c", "zip.c"}
    thirdparty_minizip_sources = (function() local __item11 = {}; for _, __item12 in ipairs(R.iter(thirdparty_minizip_sources)) do; local file = __item12; table.insert(__item11, R.add(thirdparty_minizip_dir, file)); end; return __item11 end)()
    env_thirdparty:sources(thirdparty_obj, thirdparty_minizip_sources)
    if R.truthy(R.index(env, "builtin_zstd")) then
        thirdparty_zstd_dir = "#thirdparty/zstd/"
        thirdparty_zstd_sources = {"common/debug.c", "common/entropy_common.c", "common/error_private.c", "common/fse_decompress.c", "common/pool.c", "common/threading.c", "common/xxhash.c", "common/zstd_common.c", "compress/fse_compress.c", "compress/hist.c", "compress/huf_compress.c", "compress/zstd_compress.c", "compress/zstd_double_fast.c", "compress/zstd_fast.c", "compress/zstd_lazy.c", "compress/zstd_ldm.c", "compress/zstd_opt.c", "compress/zstd_preSplit.c", "compress/zstdmt_compress.c", "compress/zstd_compress_literals.c", "compress/zstd_compress_sequences.c", "compress/zstd_compress_superblock.c", "decompress/huf_decompress.c", "decompress/zstd_ddict.c", "decompress/zstd_decompress_block.c", "decompress/zstd_decompress.c"}
        if R.truthy((function() local v = (R.contains({"android", "ios", "linuxbsd", "macos", "windows"}, R.index(env, "platform"))); if not R.truthy(v) then return v end; local v = ((R.index(env, "arch") == "x86_64")); if not R.truthy(v) then return v end; return not R.truthy(env.msvc) end)()) then
            R.append(thirdparty_zstd_sources, "decompress/huf_decompress_amd64.S")
        end
        thirdparty_zstd_sources = (function() local __item13 = {}; for _, __item14 in ipairs(R.iter(thirdparty_zstd_sources)) do; local file = __item14; table.insert(__item13, R.add(thirdparty_zstd_dir, file)); end; return __item13 end)()
        env_thirdparty:prepend({["CPPPATH"] = {thirdparty_zstd_dir, R.add(thirdparty_zstd_dir, "common")}})
        env_thirdparty:add({["CPPDEFINES"] = {"ZSTD_STATIC_LINKING_ONLY"}})
        env:prepend({["CPPPATH"] = thirdparty_zstd_dir})
        env:add({["CPPDEFINES"] = {"ZSTD_STATIC_LINKING_ONLY"}})
        env_thirdparty:sources(thirdparty_obj, thirdparty_zstd_sources)
    end
    env.core_sources = R.iadd(env.core_sources, thirdparty_obj)
    env:sources(env.core_sources, "*.cpp")
    env:generate("disabled_classes.gen.h", env:value(env.disabled_classes), env:generator(core_builders.disabled_class_builder))
    env:generate("version_generated.gen.h", env:value(methods.get_version_info(env.module_version_string)), env:generator(core_builders.version_info_builder))
    gen_hash = env:generate("version_hash.gen.cpp", env:value(methods.get_git_info()), env:generator(core_builders.version_hash_builder))
    env:sources(env.core_sources, gen_hash)
    encryption_key = R.get(os.environ, "SCRIPT_AES256_ENCRYPTION_KEY")
    encryption_key_var = "SCRIPT_AES256_ENCRYPTION_KEY"
    if R.truthy(((encryption_key == nil))) then
        encryption_key = R.get(os.environ, "GODOT_SCRIPT_ENCRYPTION_KEY")
        encryption_key_var = "GODOT_SCRIPT_ENCRYPTION_KEY"
    end
    if R.truthy(encryption_key) then
        print(R.join({"\n*** IMPORTANT: Compiling Godot with custom `", R.str(encryption_key_var), "` set as environment variable.\n*** Make sure to use templates compiled with this key when exporting a project with encryption.\n"}, ""))
    end
    gen_encrypt = env:generate("script_encryption_key.gen.cpp", env:value(encryption_key), env:generator(core_builders.encryption_key_builder))
    env:sources(env.core_sources, gen_encrypt)
    env:generate("#core/io/certs_compressed.gen.h", {"#thirdparty/certs/ca-bundle.crt", env:value(R.index(env, "builtin_certs")), env:value(R.index(env, "system_certs_path"))}, env:generator(core_builders.make_certs_header))
    env:generate("#core/authors.gen.h", "#AUTHORS.md", env:generator(core_builders.make_authors_header))
    env:generate("#core/donors.gen.h", "#DONORS.md", env:generator(core_builders.make_donors_header))
    env:generate("#core/license.gen.h", {"#COPYRIGHT.txt", "#LICENSE.txt"}, env:generator(core_builders.make_license_header))
    graph:include("profiling/recipe.lua")
    graph:include("os/recipe.lua")
    graph:include("math/recipe.lua")
    graph:include("crypto/recipe.lua")
    graph:include("io/recipe.lua")
    graph:include("debugger/recipe.lua")
    graph:include("input/recipe.lua")
    graph:include("variant/recipe.lua")
    graph:include("extension/recipe.lua")
    graph:include("object/recipe.lua")
    graph:include("templates/recipe.lua")
    graph:include("string/recipe.lua")
    graph:include("config/recipe.lua")
    graph:include("error/recipe.lua")
    lib = env:library("core", env.core_sources)
    env:prepend({["LIBS"] = {lib}})
    env:depends(lib, thirdparty_obj)
end
