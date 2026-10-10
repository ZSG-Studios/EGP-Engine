-- Standalone qualification uses the pinned vendor sources directly; no external build backend.
local root = path.absolute("../..", os.scriptdir())

option("sanitizer")
    set_default("none")
    set_values("none", "address", "undefined", "address,undefined", "thread", "memory")
    set_showmenu(true)
    set_description("Instrument all qualification sources and links; unsupported toolchains fail closed")
option_end()

rule("egp.qualification.precise")
    on_config(function (target)
        if target:has_tool("cc", "cl") then
            target:add("cxflags", "/fp:precise", {force = true})
        elseif target:has_tool("cc", "clang_cl") then
            target:add("cxflags", "/clang:-fno-fast-math", "/clang:-ffp-contract=off", {force = true})
        else
            target:add("cxflags", "-fno-fast-math", "-ffp-contract=off", {force = true})
        end
        if not target:is_plat("windows") then
            target:add("syslinks", "pthread", "m", {public = true})
        end
        local sanitizer = get_config("sanitizer") or "none"
        if sanitizer ~= "none" then
            if target:has_tool("cc", "cl", "clang_cl") then
                assert(sanitizer == "address", "Windows qualification supports only the address sanitizer")
                target:add("cxflags", "/fsanitize=address", {force = true})
                target:add("ldflags", "/INCREMENTAL:NO", "/INFERASANLIBS", {force = true})
            else
                assert(sanitizer ~= "memory" or target:has_tool("cc", "clang"), "Memory sanitizer requires Clang")
                target:add("cxflags", "-fsanitize=" .. sanitizer, "-fno-omit-frame-pointer", "-fno-sanitize-recover=all", {force = true})
                target:add("ldflags", "-fsanitize=" .. sanitizer, {force = true})
                if sanitizer == "thread" and target:is_plat("linux") then
                    target:add("cxflags", "-fPIE", {force = true})
                    target:add("ldflags", "-pie", {force = true})
                end
            end
        end
    end)
rule_end()

-- Upstream Debug tests keep Box3D's additional validation enabled; adapter qualification
-- retains the original embedded-library profile without those optional checks.
target("egp_qualification_box3d_checked")
    set_kind("static")
    set_default(false)
    set_languages("gnu17")
    add_rules("egp.qualification.precise")
    add_files(path.join(root, "thirdparty/box3d/src/*.c"))
    add_includedirs(path.join(root, "thirdparty/box3d/include"), {public = true})
    add_includedirs(path.join(root, "thirdparty/box3d/src"))
    add_defines("BOX3D_DISABLE_AVX2", "BOX3D_VALIDATE", {public = true})
    if is_plat("windows") then
        set_runtimes(is_mode("debug") and "MTd" or "MT")
    end
target_end()

for _, library in ipairs({"box2d", "box3d"}) do
    target("egp_qualification_" .. library)
        set_kind("static")
        set_default(false)
        set_languages("gnu17")
        add_rules("egp.qualification.precise")
        add_files(path.join(root, "thirdparty", library, "src/*.c"))
        add_includedirs(path.join(root, "thirdparty", library, "include"), {public = true})
        add_includedirs(path.join(root, "thirdparty", library, "src"))
        if library == "box3d" then
            add_defines("BOX3D_DISABLE_AVX2", {public = true})
            if is_plat("windows") then
                set_runtimes(is_mode("debug") and "MTd" or "MT")
            end
        else
            add_defines("BOX2D_VALIDATE", {public = true})
            -- EGP local replay sources: the pinned core.c patch routes b2Alloc/b2Free
            -- through their reservation allocator, as in the engine recipe.
            add_files(path.join(root, "thirdparty/box2d/src/local_replay/*.c"))
            add_includedirs(path.join(root, "thirdparty/box2d/src/local_replay"))
        end
    target_end()

    target("egp_qualification_" .. library .. "_shared")
        set_kind("static")
        set_default(false)
        set_languages("gnu17")
        add_rules("egp.qualification.precise")
        add_deps(library == "box3d" and "egp_qualification_box3d_checked" or "egp_qualification_box2d")
        add_files(path.join(root, "thirdparty", library, "shared/*.c"))
        add_includedirs(path.join(root, "thirdparty", library, "shared"), {public = true})
        add_includedirs(path.join(root, "thirdparty", library, "src"))
        if library == "box3d" then
            add_defines("BOX3D_INTERNAL_BENCHMARKS", {public = true})
            if is_plat("windows") then
                set_runtimes(is_mode("debug") and "MTd" or "MT")
            end
        end
    target_end()
end
