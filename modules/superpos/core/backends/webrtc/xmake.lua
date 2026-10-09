-- Experimental backend only; no TransportProvider capability is enabled here.
-- Include the pinned provider source graph before this file.
local backend_root = os.scriptdir()
local core_root = path.absolute(path.join(backend_root, "../.."))
local checked_source_root = nil

-- All C and C++ products in one link must select the same CRT.
option("superpos_rtc_runtime")
    set_description("Windows RTC CRT profile; engine integration requires an independently qualified matching profile")
    set_default("MDd")
    set_values("MDd", "MT")
    set_showmenu(true)
option_end()

option("superpos_rtc_source_root")
    set_description("New source tree produced by tools/materialize_webrtc.py")
    set_showmenu(true)
option_end()
option("superpos_rtc_test_hooks")
    set_description("Compile deterministic native backend qualification hooks")
    set_default(false)
    set_showmenu(true)
option_end()

rule("superpos.rtc.profile")
    on_load(function(target)
        import("core.base.json")
        import("core.project.config")
        assert(target:plat() == "windows" and target:arch() == "x64",
               "RTC source profile requires Windows x64")
        assert(config.get("mode") == "debug", "RTC source profile currently requires debug configuration")
        local source_root = path.absolute(assert(config.get("superpos_rtc_source_root"),
                                               "RTC source reconstruction directory is required"))
        if not checked_source_root then
            local manifest_file = path.join(backend_root, "manifest.json")
            local manifest = json.loadfile(manifest_file)
            local lock = json.loadfile(path.join(core_root, "toolchain/dependencies.lock.json"))
            local pin = lock.runtime_dependencies.libdatachannel
            assert(pin.patchset_sha256 == hash.sha256(manifest_file), "RTC manifest lock mismatch")
            local provider_root = config.get("superpos_rtc_provider_root") or path.join(backend_root, "provider-port")
            assert(pin.provider_manifest_sha256 == hash.sha256(path.join(provider_root, "providers.json")),
                   "RTC provider manifest lock mismatch")
            assert(manifest.format == 1 and #manifest.sources == 64, "Unsupported RTC source manifest")
            local receipt = json.loadfile(path.join(source_root, "materialization.json"))
            assert(receipt.format == 1 and receipt.manifest_sha256 == pin.patchset_sha256
                   and receipt.upstream_sha256 == pin.source_archive_sha256,
                   "RTC source reconstruction receipt mismatch")
            local expected = {}
            for _, row in ipairs(manifest.files) do
                assert(not expected[row.path], "Duplicate RTC source selection")
                expected[row.path] = row.sha256
                local file = path.join(source_root, row.path)
                assert(receipt.files[row.path] == row.sha256 and os.isfile(file)
                       and hash.sha256(file) == row.sha256, "RTC source/header hash mismatch: " .. row.path)
            end
            for name, digest in pairs(receipt.files) do
                assert(expected[name] == digest, "Unexpected RTC reconstruction entry: " .. name)
            end
            for _, file in ipairs(os.files(path.join(source_root, "**"))) do
                local name = path.relative(file, source_root):gsub("\\", "/")
                assert(name == "materialization.json" or expected[name], "Unselected RTC source file: " .. name)
            end
            checked_source_root = source_root
        end
        assert(source_root == checked_source_root, "Mixed RTC source roots")
        local runtime = config.get("superpos_rtc_runtime") or "MDd"
        assert(runtime == "MDd" or runtime == "MT", "Unsupported RTC CRT profile")
        target:set("runtimes", runtime)
        target:set("exceptions", "cxx")
        target:add("cxxflags", "/clang:-std=c++23", "/EHsc", "/GR", "/Z7", {force = true})
        target:add("defines", "_ITERATOR_DEBUG_LEVEL=0", "RTC_STATIC", "JUICE_STATIC",
                   "RTC_ENABLE_MEDIA=0", "RTC_ENABLE_WEBSOCKET=0", "RTC_SUPERPOS_PROFILE=1",
                   "RTC_SUPERPOS_TASK_PROFILE=1", "RTC_SUPERPOS_PROCESSOR_PROFILE=1",
                   "USE_GNUTLS=0", "USE_MBEDTLS=0", "USE_NICE=0", "WIN32_LEAN_AND_MEAN",
                   "NOMINMAX", "_CRT_SECURE_NO_WARNINGS")
        if config.get("superpos_rtc_test_hooks") then
            target:add("defines", "RTC_SUPERPOS_PROCESSOR_DIAGNOSTICS=1",
                       "RTC_SUPERPOS_PROCESSOR_TESTING=1", "RTC_SUPERPOS_PEER_PROFILE_TESTING=1",
                       "SUPERPOS_CAPTURE_TESTING=1")
        end
        target:add("includedirs", path.join(core_root, "include"), source_root,
                   path.join(source_root, "profile"), path.join(source_root, "rtc/include"),
                   path.join(source_root, "rtc/include/rtc"), path.join(source_root, "rtc/src"),
                   path.join(source_root, "rtc/src/impl"))
        target:data_set("superpos.rtc.source_root", source_root)
    end)
    before_build(function(target)
        local compiler = assert(target:tool("cxx"), "Missing RTC C++ compiler")
        local version = os.iorunv(compiler, {"--version"})
        assert(version:match("clang version ([^%s]+)") == "22.1.3", "RTC source profile requires Clang 22.1.3")
    end)
rule_end()

target("superpos_rtc_backend")
    set_kind("static")
    set_default(false)
    add_rules("superpos.rtc.profile")
    set_policy("build.ccache", false)
    add_deps("rtc_provider_openssl_ssl", "rtc_provider_openssl_common",
             "rtc_provider_libjuice", "rtc_provider_usrsctp", "rtc_provider_plog", {public = true})
    add_syslinks("ws2_32", "gdi32", "advapi32", "crypt32", "user32", "bcrypt", "iphlpapi", {public = true})
    add_files(path.join(core_root, "src/allocator.cpp"))
    on_load(function(target)
        import("core.base.json")
        local manifest = json.loadfile(path.join(backend_root, "manifest.json"))
        local root = assert(target:data("superpos.rtc.source_root"))
        local seen = {}
        for _, source in ipairs(manifest.sources) do
            assert(not seen[source], "Duplicate RTC translation unit")
            seen[source] = true
            target:add("files", path.join(root, source))
        end
    end)
target_end()
