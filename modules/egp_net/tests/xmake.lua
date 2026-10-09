set_xmakever("3.1.1")
set_project("egp-net-qualification")
add_rules("mode.debug", "mode.release")
includes("../../../misc/xmake/qualification.lua")

target("egp_net_core")
    set_kind("static")
    set_default(false)
    set_languages("c++17")
    add_rules("egp.qualification.precise")
    add_files("../net_core.cpp")
    add_includedirs("..", {public = true})
    add_deps("egp_qualification_yojimbo")
target_end()

local checks = {
    {"egp_net_checks", "core_checks.cpp", "egp_net_checks", 40},
    {"egp_net_interest_memory_check", "interest_memory_check.cpp", "egp_net_interest_memory", 15},
    {"egp_net_state_pressure_check", "state_pressure_check.cpp", "egp_net_state_pressure", 20},
    {"egp_net_fairness_check", "fairness_check.cpp", "egp_net_fairness", 45},
    {"egp_net_receive_budget_check", "receive_budget_check.cpp", "egp_net_receive_budget", 40},
    {"egp_net_replication_load_check", "replication_load_check.cpp", "egp_net_replication_load", 150},
    {"egp_net_state_encoding_check", "state_encoding_check.cpp", "egp_net_state_encoding", 45},
    {"egp_net_replication_policy_check", "replication_policy_check.cpp", "egp_net_replication_policy", 25},
    {"egp_net_replication_delta_check", "replication_delta_check.cpp", "egp_net_replication_delta", 90},
    {"egp_net_delta_codec_check", "delta_codec_check.cpp", "egp_net_delta_codec", 40}
}
for _, check in ipairs(checks) do
    target(check[1])
        set_kind("binary")
        set_languages("c++17")
        set_targetdir("$(builddir)/bin")
        add_rules("egp.qualification.precise")
        add_files(check[2])
        add_deps(check[1] == "egp_net_delta_codec_check" and "egp_qualification_yojimbo" or "egp_net_core")
        add_tests(check[3], {run_timeout = check[4] * 1000})
        if check[1] == "egp_net_fairness_check" then
            add_tests("egp_net_symmetric_fairness", {runargs = {"--symmetric"}, run_timeout = 45000})
            add_tests("egp_net_abrupt_symmetric_fairness", {runargs = {"--symmetric", "--abrupt"}, run_timeout = 60000})
            add_tests("egp_net_frame_paced_fairness", {runargs = {"--symmetric", "--frame-paced"}, run_timeout = 45000})
            add_tests("egp_net_frame_paced_abrupt_fairness", {runargs = {"--symmetric", "--abrupt", "--frame-paced"}, run_timeout = 60000})
        end
    target_end()
end

target("egp_net_process_check")
    set_kind("binary")
    set_languages("c++17")
    set_targetdir("$(builddir)/bin")
    add_rules("egp.qualification.precise")
    add_files("process_check.cpp")
    add_deps("egp_net_core")
    add_tests("egp_net_separate_processes", {run_timeout = 25000})
    on_test(function (target)
        local python = os.getenv("EGP_TEST_PYTHON") or (os.host() == "windows" and "python" or "python3")
        os.execv(python, {path.join(os.projectdir(), "process_check.py"), target:targetfile()}, {timeout = 25000})
        return true
    end)
target_end()

target("yojimbo_test")
    set_kind("binary")
    set_languages("c++17")
    set_targetdir("$(builddir)/bin")
    add_rules("egp.qualification.precise")
    add_deps("egp_qualification_yojimbo")
    add_files("../../../thirdparty/yojimbo/test.cpp", {defines = "main=egp_yojimbo_test_main"})
    add_files("../../../thirdparty/yojimbo/test_layout_debug.cpp", "../../../thirdparty/yojimbo/test_layout_release.cpp", "yojimbo_test_main.cpp")
    add_defines("SERIALIZE_ENABLE_TESTS=1")
    add_tests("yojimbo_test", {run_timeout = 180000})
target_end()

target("yojimbo_custom_packet_io_test")
    set_kind("binary")
    set_languages("c++17")
    set_targetdir("$(builddir)/bin")
    add_rules("egp.qualification.precise")
    add_deps("egp_qualification_yojimbo")
    add_files("../../../thirdparty/yojimbo/custom_packet_io_test.cpp")
    add_tests("yojimbo_custom_packet_io_test", {run_timeout = 120000})
target_end()
