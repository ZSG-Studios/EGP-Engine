-- Include this rule from the EGP native module target. No extension loader,
-- external crypto package, or alternate build backend is involved.
local module_root = os.scriptdir()
option("superpos_lifecycle_fixture")
    set_default(false)
    set_description("Native lifecycle qualification only; never enable in release packages")
    set_showmenu(true)
option_end()
-- Opt-in embedded durable recovery (default off). The EGP engine graph selects
-- it through its recipe option of the same name; this rule mirrors that
-- selection for adapter-owned graphs that apply superpos.egp.native.sources.
option("superpos_durable_recovery")
    set_default(false)
    set_description("Opt-in durable recovery services (SQLite journal, coordinator, canonical restore); requires superpos_dtls")
    set_showmenu(true)
option_end()
option("superpos_rtc_embedded")
    set_default(false)
    set_description("Experimental native RTC build closure; requires trusted native host provisioning")
    set_showmenu(true)
option_end()

-- Declare opt-in configuration before the conditional targets so a clean
-- first configure can admit all RTC arguments. Defaults remain unchanged.
option("superpos_rtc_fixture")
    set_default(false)
    set_description("Native owner-thread RTC qualification only; never enable in release packages")
    set_showmenu(true)
option_end()

option("superpos_rtc_runtime")
    set_default("MDd") set_values("MDd", "MT") set_showmenu(true)
option_end()
option("superpos_rtc_source_root")
    set_showmenu(true)
option_end()
option("superpos_rtc_test_hooks")
    set_default(false) set_showmenu(true)
option_end()

local rtc_feature = nil
local rtc_core = path.join(module_root, "core")
if has_config("superpos_rtc_embedded") then
    includes(path.join(module_root,"rtc_embedded_provider_targets.lua"))
    includes(path.join(rtc_core,"backends/webrtc/xmake.lua"))
    target("superpos_egp_rtc_backend")
        set_kind("static")
        set_default(false)
        add_rules("superpos.rtc.profile")
        add_deps("rtc_provider_plog")
        add_syslinks("ws2_32","gdi32","advapi32","crypt32","user32","bcrypt","iphlpapi",{public=true})
        add_includedirs(path.join(rtc_core,"services/include"),path.join(rtc_core,"services/src/control/include"),path.join(rtc_core,"services/src"))
        on_load(function(target)
            import("core.project.config")
            import("core.base.json")
            import("rtc_toolchain",{rootdir=module_root}).configure(target)
            local shared=json.loadfile(path.join(rtc_core,"source_manifest.json"))
            local rtc_feature=assert(shared.features.rtc_embedded,"Missing embedded RTC feature")
            assert(rtc_feature.version==1 and table.concat(rtc_feature.engine_owns,",")=="core,allocator,PSA","Unsupported embedded RTC feature")
            target:add("deps",table.unpack(rtc_feature.providers.products))
            assert(target:plat()=="windows" and target:arch()=="x64" and config.get("mode")=="debug",
                   "Embedded RTC supports Windows x64 debug only")
            assert(config.get("superpos_rtc_runtime")==rtc_feature.crt,"Embedded RTC must match engine MT CRT")
            local function enabled(value) return value==true or value=="y" or value=="true" end
            assert(enabled(config.get("threads")) and enabled(config.get("module_mbedtls_enabled")) and enabled(config.get("builtin_mbedtls")),
                   "Embedded RTC requires threaded engine-owned builtin PSA")
            local core=json.loadfile(path.join(rtc_core,"source_manifest.json"));local allocator=0
            for _,source in ipairs(core.core) do if source=="src/allocator.cpp" then allocator=allocator+1 end end
            assert(allocator==1,"Engine must own exactly one core allocator")
            local manifest=json.loadfile(path.join(rtc_core,rtc_feature.backend.manifest))
            assert(hash.sha256(path.join(rtc_core,rtc_feature.backend.manifest))==rtc_feature.backend.sha256
                   and #manifest.sources==rtc_feature.backend.source_count,"Embedded RTC backend pin mismatch")
            assert(hash.sha256(path.join(rtc_core,rtc_feature.providers.manifest))==rtc_feature.providers.sha256,"Embedded provider pin mismatch")
            local source_root=assert(target:data("superpos.rtc.source_root"));local seen={}
            for _,source in ipairs(manifest.sources) do
                assert(not seen[source] and source~="src/allocator.cpp","Duplicate/owned allocator in backend")
                seen[source]=true;target:add("files",path.join(source_root,source))
            end
            for _,source in ipairs(rtc_feature.exception_sources) do
                assert(not seen[source] and source:startswith("services/src/rtc/") and not source:find("..",1,true),"Invalid RTC exception source")
                seen[source]=true;target:add("files",path.join(rtc_core,source))
            end
        end)
        on_config(function(target)
            import("core.base.json")
            local rtc_feature=assert(json.loadfile(path.join(rtc_core,"source_manifest.json")).features.rtc_embedded)
            local compiler=assert(target:tool("cxx"));local version=os.iorunv(compiler,{"--version"})
            assert(version:match("clang version ([^%s]+)")==rtc_feature.compiler.version,"Embedded RTC requires pinned Clang")
            for _,define in ipairs(target:get("defines") or {}) do assert(define~="_HAS_EXCEPTIONS=0","Engine exception-disabled defines leaked into backend") end
            for _,dependency in ipairs(target:orderdeps()) do
                for _,forbidden in ipairs(rtc_feature.forbidden_targets) do assert(dependency:name()~=forbidden,"Standalone RTC runtime dependency forbidden") end
            end
        end)
    target_end()
end
rule("superpos.egp.native.sources")
    on_load(function (target)
        local config=import("core.project.config")
        import("core.base.json")
        local manifest = json.loadfile(path.join(module_root, "source_manifest.json"))
        local core = json.loadfile(path.join(module_root, manifest.core_manifest))
        assert(manifest.version == 1 and core.version == 1 and core.language == "c++23", "Unsupported Superpos source manifest")
        local engine_root = path.absolute("../..", module_root)
        target:set("languages", "cxx23")
        target:add("includedirs", engine_root, module_root, path.join(module_root, "core/include"))
        target:add("defines", "GODOT_MODULE", "_HAS_EXCEPTIONS=0")
        if target:is_plat("windows") then
            target:add("cxxflags", "/fp:strict", "/EHs-c-", "/GR-", "/Zc:__cplusplus", "/permissive-", {force = true})
        else
            target:add("cxxflags", "-fno-fast-math", "-ffp-contract=off", "-fno-exceptions", "-fno-rtti", {force = true})
        end
        for _, source in ipairs(manifest.sources) do
            assert(not source:find("..", 1, true) and source:endswith(".cpp"), "Invalid module source")
            target:add("files", path.join(module_root, source))
        end
        target:add("includedirs",path.join(module_root,"private/lifecycle_engine/staged"))
        if has_config("superpos_lifecycle_fixture") then
            local dev=config.get("dev_build")
            assert(config.get("egp_target")=="editor" and (dev==true or dev=="y" or dev=="true") and config.get("mode")=="debug",
                   "Lifecycle fixture requires the debug development editor")
            target:add("defines","SUPERPOS_LIFECYCLE_FIXTURE=1")
            target:add("files",path.join(module_root,"private/lifecycle_engine/staged/engine_fixture.cpp"))
        end
        local selected = table.copy(core.core)
        if has_config("superpos_rtc_embedded") then
            local rtc_feature=assert(core.features.rtc_embedded,"Missing embedded RTC feature")
            assert(rtc_feature.version==1,"Unsupported embedded RTC feature")
            target:add("deps","superpos_egp_rtc_backend",{inherit=false})
            target:add("defines","SUPERPOS_HAS_RTC=1")
            if has_config("superpos_rtc_fixture") then
                local dev=config.get("dev_build")
                assert(config.get("egp_target")=="editor" and (dev==true or dev=="y" or dev=="true") and config.get("mode")=="debug",
                       "RTC fixture requires the debug development editor")
                target:add("defines","SUPERPOS_RTC_EMBEDDED_FIXTURE=1")
            end
            target:set("runtimes",rtc_feature.crt)
            target:add("cxxflags","/clang:-std=c++23",{force=true})
            local native_feature=assert(manifest.features and manifest.features.rtc_embedded,"Missing module-owned embedded RTC source contract")
            assert(native_feature.core_feature=="rtc_embedded@1","Module/core embedded RTC version mismatch")
            for _,source in ipairs(native_feature.sources) do
                assert(source:startswith("rtc_private/") and source:endswith(".cpp") and not source:find("..",1,true),"Invalid module-owned embedded RTC source")
                target:add("files",path.join(module_root,source))
            end
            for _,directory in ipairs(rtc_feature.include_dirs) do target:add("includedirs",path.join(module_root,"core",directory)) end
            for _,source in ipairs(rtc_feature.module_sources) do
                assert(source:startswith("services/src/") and source:endswith(".cpp") and not source:find("..",1,true),"Invalid embedded RTC body source")
                target:add("files",path.join(module_root,"core",source))
            end
            for _,directory in ipairs({"godot","include","tf-psa-crypto/include","tf-psa-crypto/drivers/builtin/include"}) do
                target:add("includedirs",path.join(engine_root,"thirdparty/mbedtls",directory))
            end
            target:add("defines",'MBEDTLS_CONFIG_FILE="godot_mbedtls_config.h"','TF_PSA_CRYPTO_CONFIG_FILE="godot_psa_config.h"')
        end
        if has_config("superpos_dtls") then
            for _, source in ipairs(core.features.dtls.sources) do table.insert(selected, source) end
            for _, directory in ipairs({"godot", "include", "tf-psa-crypto/include", "tf-psa-crypto/drivers/builtin/include"}) do
                target:add("includedirs", path.join(engine_root, "thirdparty/mbedtls", directory))
            end
            target:add("defines", 'MBEDTLS_CONFIG_FILE="godot_mbedtls_config.h"', 'TF_PSA_CRYPTO_CONFIG_FILE="godot_psa_config.h"', "SUPERPOS_HAS_DTLS=1")
            if target:is_plat("windows") then target:add("syslinks", "ws2_32", "bcrypt") end
        end
        if has_config("superpos_durable_recovery") then
            assert(has_config("superpos_dtls"), "Durable recovery borrows EGP PSA through superpos_dtls")
            local recovery = assert(manifest.features and manifest.features.durable_recovery, "Missing module-owned durable recovery contract")
            assert(recovery.version == 1 and recovery.core_feature == "services", "Unsupported durable recovery contract")
            local services = assert(core.features and core.features.services, "Core mirror lacks the services feature")
            for _, directory in ipairs({"core/services/include", "core/services/src/control/include", "core/services/src", recovery.sqlite.directory}) do
                target:add("includedirs", path.join(module_root, directory))
            end
            target:add("defines", "SUPERPOS_HAS_DURABLE_RECOVERY=1")
            for _, source in ipairs(services.sources) do
                assert(source:startswith("services/src/") and source:endswith(".cpp") and not source:find("..", 1, true), "Invalid services source")
                target:add("files", path.join(module_root, "core", source))
            end
            for _, source in ipairs(recovery.sources) do
                assert(source:startswith("private/recovery/") and source:endswith(".cpp") and not source:find("..", 1, true), "Invalid durable recovery source")
                target:add("files", path.join(module_root, source))
            end
            local pin = json.loadfile(path.join(module_root, recovery.sqlite.manifest))
            assert(pin.version == recovery.sqlite.version, "SQLite pin differs from the durable recovery contract")
            for _, source in ipairs(pin.sources) do
                assert(hash.sha256(path.join(module_root, recovery.sqlite.directory, source.path)) == source.sha256, "SQLite amalgamation differs from its pin: " .. source.path)
            end
            target:add("files", path.join(module_root, recovery.sqlite.directory, "sqlite3.c"), {defines = pin.build_definitions, warnings = "none"})
        end
        for _, source in ipairs(selected) do
            assert(source:startswith("src/") and not source:find("..", 1, true) and source:endswith(".cpp"), "Invalid core source")
            target:add("files", path.join(module_root, "core", source))
        end
    end)
rule_end()
