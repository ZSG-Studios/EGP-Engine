-- Managed command construction follows the matching native precision and API contract.
function main()
    local managed = import("build.xmake.managed", {rootdir = os.projectdir()})
    local checks = 0
    local function check(value) assert(value); checks = checks + 1 end
    local options = managed.parse_options({"no-deprecated=false", "werror", "dev-debug=n"})
    check(options["no-deprecated"] == false and options.werror == true and options["dev-debug"] == false)
    local args = managed.build_arguments("matching.sln", "Debug", "double", options, {"/p:NoWarn=1591"})
    check(args[1] == "msbuild" and args[2] == "matching.sln")
    check(table.contains(args, "/p:GodotFloat64=true") and table.contains(args, "/p:TreatWarningsAsErrors=true"))
    check(not table.contains(args, "/p:GodotNoDeprecated=true"))
    args = managed.build_arguments("matching.sln", "Release", "single", managed.parse_options({"no-deprecated"}))
    check(table.contains(args, "/p:GodotNoDeprecated=true") and not table.contains(args, "/p:GodotFloat64=true"))
    check(not utils.trycall(function() managed.parse_options({"unknown"}) end))
    check(not utils.trycall(function() managed.parse_options({"push-nupkgs-local"}) end))
    check(not utils.trycall(function() managed.parse_options({"werror=maybe"}) end))
    local model = import("build.xmake.graph", {rootdir = os.projectdir()})
    local launcher = import("misc.scripts.build_egp", {rootdir = os.projectdir()})
    local filename
    for _, arch in ipairs({"x86_64", "x64", "amd64"}) do
        local graph = model.new(os.projectdir(), {platform="windows", arch=arch, target="editor", module_mono_enabled="1", dev_build="1"})
        graph:configure()
        local result = launcher.build_result(os.projectdir(), graph:serialize())
        check(result.mono and result.arch == "x86_64" and result.precision == "single")
        check(path.filename(result.editor) == "godot.windows.editor.dev.x86_64.mono.exe")
        filename = filename or result.editor
        check(result.editor == filename)
    end
    local graph = model.new(os.projectdir(), {platform="windows", arch="x64", target="editor", module_mono_enabled="0", dev_build="1"})
    graph:configure()
    local result = launcher.build_result(os.projectdir(), graph:serialize())
    check(not result.mono and path.filename(result.editor) == "godot.windows.editor.dev.x86_64.exe")
    for _, platform in ipairs({"windows", "linuxbsd", "macos", "android", "ios", "visionos"}) do
        check(managed.validate_platform(platform) == platform)
    end
    check(not utils.trycall(function() managed.validate_platform("web") end))
    print("NATIVE_MANAGED_CONTRACT_CHECKS=" .. checks)
end
