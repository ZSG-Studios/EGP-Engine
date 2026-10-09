set_xmakever("3.1.1")
set_project("egp-box3d-qualification")
add_rules("mode.debug", "mode.release")
includes("../../../misc/xmake/qualification.lua")

for _, check in ipairs({"determinism", "joints"}) do
    target("egp_box3d_" .. check)
        set_kind("binary")
        set_languages("c++17")
        set_targetdir("$(builddir)/bin")
        add_rules("egp.qualification.precise")
        add_files(check .. ".cpp")
        add_deps("egp_qualification_box3d")
        if is_plat("windows") then
            set_runtimes(is_mode("debug") and "MTd" or "MT")
        end
        if check == "determinism" then
            add_files("../../../modules/box3d/deterministic_world.cpp", "../../../modules/box3d/egp_box3d_api.cpp")
            add_includedirs("../../../modules/box3d")
            add_defines('EGP_BOX3D_GOLDEN_PATH="' .. path.join(os.scriptdir(), "golden_hashes.txt"):gsub("\\", "/") .. '"')
        end
        add_tests(check, {run_timeout = 90000})
    target_end()
end

target("egp_box3d_upstream")
    set_kind("binary")
    set_default(false)
    set_languages("c17")
    set_targetdir("$(builddir)/bin")
    add_rules("egp.qualification.precise")
    add_deps("egp_qualification_box3d_shared")
    add_files("../../../thirdparty/box3d/test/*.c")
    add_includedirs("../../../thirdparty/box3d/src")
    if is_plat("windows") then
        set_runtimes(is_mode("debug") and "MTd" or "MT")
    end
    add_tests("upstream", {run_timeout = 120000})
target_end()
