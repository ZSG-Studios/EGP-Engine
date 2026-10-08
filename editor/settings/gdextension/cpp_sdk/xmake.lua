set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags", false)
add_rules("mode.debug", "mode.release")
option("egp_cpp_cache")
    set_default("")
    set_showmenu(true)
option_end()
option("egp_cpp_library")
    set_default("")
    set_showmenu(true)
option_end()
local sdk_root = os.scriptdir()
target("godot-cpp")
    set_kind("static")
    set_basename("egp_godot_cpp")
    set_languages("cxx17")
    set_policy("build.across_targets_in_parallel", true)
    add_files(path.join(sdk_root, "src/**.cpp"), path.join(sdk_root, "gen/src/**.cpp"))
    add_includedirs(path.join(sdk_root, "include"), path.join(sdk_root, "gen/include"), {public = true})
    add_defines("GDEXTENSION", "THREADS_ENABLED", @PRECISION_DEFINE@ {public = true})
    if is_mode("debug") then
        add_defines("DEBUG_ENABLED", "HOT_RELOAD_ENABLED", {public = true})
    end
    if is_plat("windows") then
        add_defines("TYPED_METHOD_BIND", "NOMINMAX", {public = true})
        add_cxxflags("/utf-8", "/Zc:__cplusplus", {tools = {"cl", "clang_cl"}, public = true})
        set_runtimes(is_mode("debug") and "MDd" or "MD")
        if is_mode("debug") then
            -- A shared archive must carry its debug information instead of
            -- depending on a per-project PDB path in the compiler/cache key.
            set_symbols("debug", "embed")
        end
    else
        add_cxxflags("-fPIC", "-fvisibility=hidden", {public = true})
    end
    on_config(function (target)
        local bits = (target:arch() == "x86" or target:arch() == "arm" or target:arch() == "wasm32" or target:arch() == "riscv32") and 32 or 64
        assert(bits == @BITS@, "Bundled SDK architecture mismatch: use an editor built for this target")
        local prebuilt = get_config("egp_cpp_library")
        if prebuilt and prebuilt ~= "" then
            assert(os.isfile(prebuilt), "Requested prebuilt matching SDK library is missing")
            target:set("kind", "phony")
            target:del("files", target:get("files"))
            -- Public links propagate to both shared and executable consumers;
            -- ldflags are not the shared-library linker flags in native xmake.
            target:add("links", path.absolute(prebuilt), {public = true})
            target:data_set("egp_cache_hit", true)
            return
        end
        import("core.tool.compiler")
        import("lib.detect.find_tool")
        import("core.base.bytes")
        local function compact_objects()
            -- MSVC resolves a relative /OUT against the deep game-project cwd
            -- before normalizing it. Keep the short external archive absolute.
            if target:is_plat("windows") then
                target:set("targetdir", path.absolute(target:targetdir(), os.projectdir()))
            end
            import("tools.generated_objects", {rootdir = sdk_root}).apply(target, path.join(sdk_root, "gen"))
        end
        local cache = get_config("egp_cpp_cache")
        if not cache or cache == "" then compact_objects(); return end
        local cc = assert(compiler.load("cxx", target))
        local tool = find_tool(cc:name(), {program = cc:program(), version = true})
        -- Never reuse an archive when its compiler identity cannot be established.
        if not tool or not tool.version then compact_objects(); return end
        local flags = table.concat(cc:compflags({target = target}), " ")
        -- Host SDK/toolset changes invalidate cache entries even when clang's
        -- version string is unchanged. Never log these environment values.
        local sdk_identity = table.concat({os.getenv("INCLUDE") or "", os.getenv("LIB") or "", os.getenv("VCToolsVersion") or "",
            os.getenv("WindowsSDKVersion") or "", os.getenv("SDKROOT") or "", os.getenv("DEVELOPER_DIR") or ""}, "\n")
        local key = hash.sha256(bytes(io.readfile(path.join(sdk_root, "sdk.json")) .. cc:program() .. tool.version .. target:plat() .. target:arch() .. get_config("mode") .. flags .. sdk_identity))
        local cached = path.join(cache, key, path.filename(target:targetfile()))
        target:data_set("egp_cache_file", cached)
        if os.isfile(cached) then
            target:set("kind", "phony")
            target:del("files", target:get("files"))
            target:add("linkdirs", path.directory(cached), {public = true})
            target:add("links", "egp_godot_cpp", {public = true})
            target:data_set("egp_cache_hit", true)
        else
            compact_objects()
        end
    end)
    after_build(function (target)
        local destination = target:data("egp_cache_file")
        if destination and not target:data("egp_cache_hit") then
            import("tools.publish_cache", {rootdir = sdk_root}).main(target:targetfile(), destination)
        end
    end)
target_end()
