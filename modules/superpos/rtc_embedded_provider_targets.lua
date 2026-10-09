-- Embedded-only target declarations derived from pinned provider recipes.
-- Global project policies are intentionally absent.
-- Proposed Windows provider source graph. No prebuilt archive is consumed.

local provider_root = path.join(os.scriptdir(),"core/backends/webrtc/provider-port")
local cached_manifest = nil


local function checked_profile()
    set_exceptions("no-cxx")
    before_build(function(target)
        local config = import("core.project.config", {anonymous = true})
        assert(target:plat() == "windows" and target:arch() == "x64", "RTC provider proposal supports Windows x64 only")
        assert(config.get("mode") == "debug", "RTC provider profile currently requires debug configuration")
        local compiler = assert(target:tool("cc"), "Missing native C compiler")
        local version = os.iorunv(compiler, {"--version"})
        assert(version:match("clang version ([^%s]+)") == "22.1.3", "RTC provider proposal requires Clang 22.1.3")
    end)
end

for _, name in ipairs({"rtc_provider_openssl_crypto", "rtc_provider_openssl_ssl",
                       "rtc_provider_openssl_common", "rtc_provider_libjuice", "rtc_provider_usrsctp"}) do
    target(name)
        set_default(false)
        add_rules("mode.debug","mode.release")
        set_policy("check.auto_ignore_flags",false)
        set_policy("build.ccache",false)
        set_kind("static")
        checked_profile()
        on_load(function(target)
            import("core.project.config")
            local runtime = config.get("superpos_rtc_runtime") or "MDd"
            assert(runtime == "MDd" or runtime == "MT", "Unsupported RTC provider CRT profile")
            target:set("runtimes", runtime)
            import("core.base.json")
            if not cached_manifest then
                local selected = json.loadfile(path.join(provider_root, "providers.json"))
                assert(selected.version == 1, "Unsupported RTC provider source selection")
                local checked = 0
                for _, package in pairs(selected.packages) do
                    for relative, expected in pairs(package.files) do
                        assert(not path.is_absolute(relative) and not relative:find("..", 1, true), "Unsafe RTC provider selected path")
                        local file = path.join(provider_root, package.root, relative)
                        assert(os.isfile(file) and hash.sha256(file) == expected, "RTC provider selected source/header hash mismatch: " .. file)
                        checked = checked + 1
                    end
                    local source_root = path.join(provider_root, package.root)
                    for _, file in ipairs(os.files(path.join(source_root, "**"))) do
                        local relative = path.relative(file, source_root):gsub("\\", "/")
                        assert(package.files[relative], "Unselected RTC provider input: " .. file)
                    end
                end
                assert(checked == 1891, "RTC provider selected file count changed")
                cached_manifest = selected
            end
            local manifest = cached_manifest
            local product = assert(manifest.products[target:name()], "Missing RTC provider product")
            for _, flag in ipairs(product.cflags) do target:add("cflags", flag, {force = true}) end
            for _, define in ipairs(product.definitions) do target:add("defines", define) end
            for _, dependency in ipairs(product.dependencies) do target:add("deps", dependency) end
            for _, library in ipairs(product.syslinks) do target:add("syslinks", library, {public = true}) end
            local source_root = path.join(provider_root, manifest.packages[product.package].root)
            for _, item in ipairs(product.sources) do
                local includes = {}
                for _, include in ipairs(item.includes) do
                    table.insert(includes, path.join(source_root, include))
                end
                target:add("files", path.join(source_root, item.source), {defines = item.defines, includedirs = includes})
            end
            if product.package == "openssl" or product.package == "libjuice" then
                target:add("includedirs", path.join(source_root, "include"), {public = true})
            end
            if product.package == "usrsctp" then
                target:add("includedirs", path.join(source_root, "usrsctplib"), {public = true})
            end
        end)
    target_end()
end

target("rtc_provider_plog")
    set_default(false)
    set_kind("headeronly")
    add_includedirs(path.join(provider_root, "vendor/plog/include"), {public = true})
    on_load(function(target)
            import("core.base.json")
            if not cached_manifest then
                local selected = json.loadfile(path.join(provider_root, "providers.json"))
                assert(selected.version == 1, "Unsupported RTC provider source selection")
                local checked = 0
                for _, package in pairs(selected.packages) do
                    for relative, expected in pairs(package.files) do
                        assert(not path.is_absolute(relative) and not relative:find("..", 1, true), "Unsafe RTC provider selected path")
                        local file = path.join(provider_root, package.root, relative)
                        assert(os.isfile(file) and hash.sha256(file) == expected, "RTC provider selected source/header hash mismatch: " .. file)
                        checked = checked + 1
                    end
                    local source_root = path.join(provider_root, package.root)
                    for _, file in ipairs(os.files(path.join(source_root, "**"))) do
                        local relative = path.relative(file, source_root):gsub("\\", "/")
                        assert(package.files[relative], "Unselected RTC provider input: " .. file)
                    end
                end
                assert(checked == 1891, "RTC provider selected file count changed")
                cached_manifest = selected
            end
            local manifest = cached_manifest
    end)
target_end()
