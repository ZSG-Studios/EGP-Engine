set_xmakever("3.1.1")
includes("../../platforms/visionos.lua")

target("visionos_configuration_probe")
    set_kind("phony")
    on_config(function ()
        import("core.project.project")
        import("core.sandbox.sandbox")
        local checks = 0
        local function check(value, message)
            assert(value, message)
            checks = checks + 1
        end
        for _, simulator in ipairs({false, true}) do
            -- Run the production callbacks on xmake's real toolchain instance.
            -- Only Xcode discovery is simulated, since this contract runs on all hosts.
            local instance = project.toolchain("egp-visionos", {plat="cross", arch="arm64", simulator=simulator})
            -- Repeat the discovery regression even when a previous configure saved this instance.
            instance:config_set("__checked", nil)
            local environment = sandbox.fork(instance:info():get("check"))
            local discovery_calls = 0
            local sdk = simulator and "xrsimulator" or "xros"
            local sdkroot = os.projectdir()
            local fake_os = table.inherit(os)
            fake_os.iorunv = function (program, args)
                check(program == "fixture-xcrun" and args[1] == "--sdk" and args[2] == sdk, "Wrong Xcode SDK discovery request")
                discovery_calls = discovery_calls + 1
                if args[3] == "--show-sdk-path" then return sdkroot end
                check(args[3] == "--find", "Unknown Xcode discovery operation")
                return path.join(sdkroot, args[4])
            end
            environment:api_register_builtin("os", fake_os)
            environment:api_register_builtin("find_tool", function (name)
                check(name == "xcrun", "Only Xcode discovery may be simulated")
                return {program="fixture-xcrun"}
            end)
            environment:api_register_builtin("import", function (name)
                check(name == "lib.detect.find_tool", "Unexpected production callback import")
            end)
            check(instance:check(), "Production visionOS availability check failed")
            check(instance:config("egp_sdkroot") == sdkroot, "SDK root was not retained on the real instance")
            for _, tool in ipairs({"clang", "clangxx", "ar"}) do
                local executable = tool == "clangxx" and "clang++" or tool
                check(instance:config("egp_" .. tool) == path.join(sdkroot, executable), "Discovered compiler path was not retained")
            end
            check(discovery_calls == 4, "All four Xcode discovery commands must run")
            check(instance:check() and discovery_calls == 4, "Repeated availability check must use xmake's cached result")
            instance:load()
            local triple = "arm64-apple-xros26.0" .. (simulator and "-simulator" or "")
            for _, key in ipairs({"cxflags", "mxflags", "ldflags", "shflags"}) do
                local flags = table.wrap(instance:get(key))
                check(table.contains(flags, triple) and table.contains(flags, sdkroot), "Compiler and linker must use the selected visionOS SDK and target")
            end
            check(instance:get("toolset.cxx") == path.join(sdkroot, "clang++"), "Toolchain load must retain the discovered C++ compiler")
        end
        print("NATIVE_VISIONOS_TOOLCHAIN_CHECKS=" .. checks)
    end)
target_end()
