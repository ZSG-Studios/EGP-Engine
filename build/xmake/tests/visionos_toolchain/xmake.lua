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
        local policy = import("build.xmake.platforms.init", {rootdir=path.absolute("../../../..", os.scriptdir())})
        for _, profile in ipairs({{arch="arm64", simulator=false}, {arch="arm64", simulator=true}, {arch="x86_64", simulator=true}}) do
            local simulator = profile.simulator
            -- Run the production callbacks on xmake's real toolchain instance.
            -- Only Xcode discovery is simulated, since this contract runs on all hosts.
            local instance = project.toolchain("egp-visionos", {plat="cross", arch=profile.arch, simulator=simulator})
            -- Repeat the discovery regression even when a previous configure saved this instance.
            instance:config_set("__checked", nil)
            local environment = sandbox.fork(instance:info():get("check"))
            local discovery_calls = 0
            local sdk = simulator and "xrsimulator" or "xros"
            local sdkroot = path.join(import("core.project.config").builddir(), "fixture-sdk26.5")
            os.mkdir(sdkroot)
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
            for _, tool in ipairs({"clang", "clangxx", "ar", "swiftc"}) do
                local executable = tool == "clangxx" and "clang++" or tool
                check(instance:config("egp_" .. tool) == path.join(sdkroot, executable), "Discovered compiler path was not retained")
            end
            check(discovery_calls == 5, "All five Xcode discovery commands must run")
            check(instance:check() and discovery_calls == 5, "Repeated availability check must use xmake's cached result")
            instance:load()
            local triple = profile.arch .. "-apple-xros26.0" .. (simulator and "-simulator" or "")
            for _, key in ipairs({"cxflags", "mxflags", "ldflags", "shflags"}) do
                local flags = table.wrap(instance:get(key))
                check(table.contains(flags, triple) and table.contains(flags, sdkroot), "Compiler and linker must use the selected visionOS SDK and target")
            end
            local probe = {values={}}
            function probe:set(key, ...) self.values[key] = {...} end
            function probe:add(key, ...)
                self.values[key] = self.values[key] or {}
                for _, value in ipairs({...}) do
                    if type(value) ~= "table" then table.insert(self.values[key], value) end
                end
            end
            policy.configure(probe, {platform="visionos", arch=profile.arch, simulator=simulator, accesskit=false, vulkan=false, opengl3=false, metal=false, sdl=false})
            for _, key in ipairs({"cxflags", "mxflags", "ldflags", "shflags"}) do
                local combined = table.join(table.wrap(instance:get(key)), probe.values[key] or {})
                local targets = 0
                for index, value in ipairs(combined) do
                    check(not value:startswith("-mtargetos"), "Platform policy must not conflict with the custom compiler target")
                    if value == "-target" then
                        targets = targets + 1
                        check(combined[index + 1] == triple, "Deployment must remain 26.0 even with a newer SDK")
                    end
                end
                check(targets == 1, "Compiler/linker must have exactly one explicit visionOS target")
            end
            import("lib.detect.find_tool")
            local clang = find_tool("clang++")
            if not clang and os.host() == "windows" then
                local candidates = os.files("C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/Llvm/x64/bin/clang++.exe")
                if #candidates > 0 then clang = {program=candidates[1]} end
            end
            assert(clang, "The combined Apple driver regression requires Clang")
            local source = path.join(sdkroot, "empty.cpp")
            io.writefile(source, "static_assert(sizeof(int) >= 2);\n")
            local compiler_flags = table.join(table.wrap(instance:get("cxflags")), probe.values.cxflags or {})
            local args = table.join(compiler_flags, {"-fsyntax-only", "-nostdinc", source})
            os.iorunv(clang.program, args)
            check(true, "Combined production visionOS flags must compile through Clang's real driver")
            local old_args = table.join(args, {"-mtargetos=xros26.0" .. (simulator and "-simulator" or "")})
            local accepted, failure = true, nil
            try {function () os.iorunv(clang.program, old_args) end, catch {function (errors) accepted, failure = false, errors end}}
            check(not accepted and tostring(failure):find("cannot specify", 1, true), "Negative control must reproduce Clang's duplicate deployment rejection")
            check(instance:get("toolset.cxx") == path.join(sdkroot, "clang++"), "Toolchain load must retain the discovered C++ compiler")
            check(instance:get("toolset.sc") == path.join(sdkroot, "swiftc"), "Swift WMO compilation must resolve its compiler")
            local swiftflags = table.wrap(instance:get("scflags"))
            check(table.contains(swiftflags, triple) and table.contains(swiftflags, "-sdk") and table.contains(swiftflags, sdkroot), "Swift must use the selected visionOS SDK and target")
        end
        for _, simulator in ipairs({false, true}) do
            for _, opengl in ipairs({false, true}) do
                local probe = {values={}}
                function probe:set(key, ...) self.values[key] = {...} end
                function probe:add(key, ...)
                    self.values[key] = self.values[key] or {}
                    for _, value in ipairs({...}) do
                        if type(value) ~= "table" then table.insert(self.values[key], value) end
                    end
                end
                policy.configure(probe, {platform="ios", arch="arm64", simulator=simulator, accesskit=false, vulkan=false, opengl3=opengl, metal=false, sdl=false, werror=true})
                check(table.contains(probe.values.defines, "GLES_SILENCE_DEPRECATION") == opengl, "iOS OpenGLES SDK deprecation opt-out must follow the selected renderer")
                for _, key in ipairs({"cxflags", "ldflags", "shflags"}) do
                    check(table.contains(probe.values[key], simulator and "-mios-simulator-version-min=15.0" or "-miphoneos-version-min=15.0"), "iOS deployment flags must remain unchanged")
                end
                check(table.contains(probe.values.cxflags, "-Werror"), "OpenGLES compatibility must preserve general warning enforcement")
            end
        end
        local invalid = project.toolchain("egp-visionos", {plat="cross", arch="x86_64", simulator=false})
        local accepted, failure = true, nil
        try {function () invalid:load() end, catch {function (errors) accepted, failure = false, errors end}}
        check(not accepted and tostring(failure):find("simulator-only", 1, true), "x86_64 device builds must be rejected")
        print("NATIVE_VISIONOS_TOOLCHAIN_CHECKS=" .. checks)
    end)
target_end()
