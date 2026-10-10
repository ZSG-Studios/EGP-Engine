-- The launcher result accepts each platform's declared primary product and nothing else.
function main()
    local model = import("build.xmake.graph", {rootdir = os.projectdir()})
    local launcher = import("misc.scripts.build_egp", {rootdir = os.projectdir()})
    local checks = 0
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local function result(options)
        local graph = model.new(os.projectdir(), options)
        graph:configure()
        return launcher.build_result(os.projectdir(), graph:serialize()), graph
    end
    local windows = result({platform="windows", arch="x86_64", target="editor", module_mono_enabled="0", dev_build="1"})
    check(path.filename(windows.editor) == "godot.windows.editor.dev.x86_64.exe", "Windows editor executable")
    local android, graph = result({platform="android", arch="arm64", target="template_debug", module_mono_enabled="0"})
    check(graph.programs[1].kind == "shared", "Android primary product is a shared library")
    check(path.filename(android.editor) == "libgodot.android.template_debug.arm64.so", "Android shared library product")
    check(android.platform == "android" and android.target == "template_debug" and android.arch == "arm64" and not android.mono)
    check(path.directory(android.editor) == path.join(os.projectdir(), "bin"), "Products stay in the canonical bin directory")
    -- Clang is the default and only Linux toolchain: canonical Linux names carry no .llvm marker.
    local linux, linux_graph = result({platform="linuxbsd", arch="x86_64", target="editor", module_mono_enabled="0", dev_build="1"})
    check(linux_graph.options.use_llvm == true, "Linux defaults to the Clang toolchain")
    check(path.filename(linux.editor) == "godot.linuxbsd.editor.dev.x86_64", "Linux editor name has no .llvm suffix")
    local clang_windows = result({platform="windows", arch="x86_64", target="editor", module_mono_enabled="0", dev_build="1", use_llvm="1"})
    check(path.filename(clang_windows.editor) == "godot.windows.editor.dev.x86_64.llvm.exe", "Windows clang-cl products keep their .llvm marker")
    local valid = launcher.valid_program_filename
    check(valid({kind="binary", filename="godot.linuxbsd.editor.dev.x86_64"}))
    check(valid({kind="shared", filename="libgodot.linuxbsd.template_release.x86_64.so"}))
    check(valid({kind="static", filename="godot.windows.template_release.x86_64.lib"}))
    for _, program in ipairs({
        {kind="binary", filename="libgodot.android.template_debug.arm64.so"},
        {kind="shared", filename="evil.so"},
        {kind="shared", filename="../libgodot.android.so"},
        {kind="shared", filename="lib/libgodot.android.so"},
        {kind="binary", filename="godot..exe"},
        {kind="binary", filename=""},
        {kind="binary"},
        {filename="godot.windows.editor.x86_64.exe"},
        {kind="plugin", filename="godot.windows.editor.x86_64.exe"}}) do
        check(not valid(program), "Rejected program filename: " .. tostring(program.filename))
        local serialized = {options = {platform="android", target="template_debug", arch="arm64"}, programs = {program}, libraries = {}}
        check(not utils.trycall(function() launcher.build_result(os.projectdir(), serialized) end), "build_result must reject it")
    end
    print("NATIVE_BUILD_RESULT_CHECKS=" .. checks)
end
