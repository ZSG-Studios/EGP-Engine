-- Exercise the packaged SDK recipe's actual MSVC archive output at a deep cwd.
function main()
    if os.host() ~= "windows" then return end
    local root = os.curdir()
    local recipe = assert(io.readfile(path.join(root, "editor/settings/gdextension/cpp_sdk/xmake.lua")))
    local directory = path.join(root, ".build/sdk-archive-path")
    local native = path.join(root, ".build/sdk-archive-native")
    local project = path.join(directory, "deep-game-project-with-spaces", "nested-extension-project", "additional-project-path-segment",
        "extra-nesting-to-exercise-windows-raw-output-path-limit", "extension")
    local sdk = path.join(directory, "sdk")
    os.mkdir(project); os.mkdir(path.join(sdk, "src")); os.mkdir(path.join(sdk, "tools"))
    os.mkdir(path.join(sdk, "include")); os.mkdir(path.join(sdk, "gen/include")); os.mkdir(path.join(sdk, "gen/src"))
    io.writefile(path.join(sdk, "src/probe.cpp"), "int egp_archive_path_probe() { return 42; }\n")
    io.writefile(path.join(sdk, "gen/src/probe.cpp"), "int egp_generated_archive_probe() { return 7; }\n")
    os.cp(path.join(root, "build/xmake/generated_objects.lua"), path.join(sdk, "tools/generated_objects.lua"))
    io.writefile(path.join(project, "xmake.lua"), 'includes("' .. path.join(sdk, "xmake.lua"):gsub("\\", "/") .. '")\n')
    recipe = recipe:gsub("@PRECISION_DEFINE@", ""):gsub("@BITS@", "64")
    local checks = 0
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local function invoke(arguments)
        return os.iorunv(os.programfile(), arguments, {curdir = project, timeout = 60000,
            envs = {XMAKE_CONFIGDIR = path.join(directory, "config"), XMAKE_GLOBALDIR = path.join(directory, "global")}})
    end
    local configure = {"f", "-y", "-P", project, "-o", native, "-p", "windows", "-a", "x64", "--toolchain=msvc", "-m", "debug"}
    -- The old recipe compiles the TU but MSVC cannot open its relative /OUT.
    local old = recipe:gsub('            %-%- MSVC resolves a relative /OUT.-            end\n', '')
    check(old ~= recipe, "Negative control must remove only output normalization")
    io.writefile(path.join(sdk, "xmake.lua"), old)
    invoke(configure)
    local ok, failure = utils.trycall(function () return invoke({"-P", project, "-b", "-r", "-j", "1", "-vD", "godot-cpp"}) end)
    io.writefile(path.join(directory, "old-output-negative.log"), tostring(failure))
    check(not ok and tostring(failure):find("LNK1104", 1, true), "Old relative archive output must reproduce the actual linker failure")
    io.writefile(path.join(sdk, "xmake.lua"), recipe)
    invoke(configure)
    local output = invoke({"-P", project, "-b", "-j", "1", "-vD", "godot-cpp"})
    io.writefile(path.join(directory, "absolute-output.log"), output)
    local archive = path.join(native, "windows/x64/debug/egp_godot_cpp.lib")
    check(os.isfile(archive) and os.filesize(archive) > 0, "Corrected packaged SDK recipe must publish a real archive")
    check(output:lower():find("-out:" .. archive:lower():gsub("/", "\\"), 1, true) ~= nil,
        "Installed MSVC archive argv must contain the absolute target path")
    local relative = path.relative(archive, project)
    check(#project + 1 + #relative >= 260 and #archive < 240, "Fixture must expose raw Windows path overflow with a short absolute output")
    local before = {mtime = os.mtime(archive), hash = hash.sha256(archive)}
    invoke({"-P", project, "-b", "-j", "1", "godot-cpp"})
    check(os.mtime(archive) == before.mtime and hash.sha256(archive) == before.hash, "Warm archive must remain unchanged")
    print("NATIVE_SDK_ARCHIVE_PATH_CHECKS=" .. checks)
end
