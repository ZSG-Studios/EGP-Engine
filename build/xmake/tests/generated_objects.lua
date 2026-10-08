-- Compile generated sources inside the ordinary long wrapper variant layout.
function main()
    local root = os.projectdir()
    local project = path.join(root, "build/xmake/tests/generated_objects")
    local directory = path.join(root, ".build/xmake-cache/windows-x86_64-template_debug-9889766c32d9/object-path-regression")
    local envs = {XMAKE_CONFIGDIR = path.join(directory, "config"), XMAKE_GLOBALDIR = path.join(root, ".build/xmake-global/contract-generated-objects")}
    local compiler = assert(({windows = "msvc", linux = "gcc", macosx = "xcode", bsd = "clang"})[os.host()])
    local function invoke(name, args)
        local stdout, stderr = os.iorunv(os.programfile(), args, {curdir = project, envs = envs, timeout = 120000})
        os.mkdir(directory); io.writefile(path.join(directory, name .. ".log"), stdout .. (stderr or ""))
        return stdout .. (stderr or "")
    end
    local text = invoke("configure", {"f", "-y", "--toolchain=" .. compiler, "-P", project, "-o", directory, "-m", "debug"})
    text = invoke("build", {"-P", project, "-b", "-j", "1"})
    assert(text:find("NATIVE_GENERATED_OBJECT_GRAPH_CHECKS=11", 1, true))
    local snapshots = {}
    local object_count = 0
    for _, object in ipairs(import("core.base.json").loadfile(path.join(directory, "linked-objects.json"))) do
        if object:endswith(".obj") or object:endswith(".o") then
            object = path.absolute(object, project)
            snapshots[object] = {mtime = os.mtime(object), hash = hash.sha256(object)}
            object_count = object_count + 1
        end
    end
    assert(object_count == 4, "All four actual translation units must compile")
    invoke("warm", {"-P", project, "-b", "-j", "1"})
    for object, state in pairs(snapshots) do
        assert(os.mtime(object) == state.mtime and hash.sha256(object) == state.hash, "Warm build unexpectedly rebuilt an object")
    end
    invoke("run", {"run", "-P", project, "egp_generated_object_probe"})
    invoke("configure-release", {"f", "-y", "--toolchain=" .. compiler, "-P", project, "-o", directory, "-m", "release"})
    local release_text = invoke("build-release", {"-P", project, "-b", "-j", "1"})
    assert(release_text:find("NATIVE_GENERATED_OBJECT_GRAPH_CHECKS=11", 1, true))
    local release_objects, compact_count = {}, 0
    for _, object in ipairs(import("core.base.json").loadfile(path.join(directory, "linked-objects.json"))) do
        object = path.absolute(object, project)
        release_objects[object] = {mtime = os.mtime(object), hash = hash.sha256(object)}
        if path.directory(object) == path.join(directory, "generated-objects") then
            assert(not snapshots[object], "Debug and Release compact objects must have distinct paths in the same build directory")
            compact_count = compact_count + 1
        end
    end
    assert(compact_count == (os.host() == "windows" and 3 or 2))
    invoke("warm-release", {"-P", project, "-b", "-j", "1"})
    for object, state in pairs(release_objects) do
        assert(os.mtime(object) == state.mtime and hash.sha256(object) == state.hash, "Warm Release build unexpectedly rebuilt an object")
    end
    for object, state in pairs(snapshots) do
        if path.directory(object) == path.join(directory, "generated-objects") then
            assert(os.mtime(object) == state.mtime and hash.sha256(object) == state.hash, "Release build overwrote a Debug compact object")
        end
    end
    invoke("run-release", {"run", "-P", project, "egp_generated_object_probe"})
    local external = path.join(root, ".build/generated-object-external-source")
    local deep_project = path.join(directory, "deep-project", "nested-extension-project-with-external-sdk-source", "nested-external-path-regression")
    local deep_build = path.join(directory, "external-build")
    os.mkdir(external); os.mkdir(deep_project)
    local external_source = path.join(external, "source.cpp")
    local external_header = path.join(external, "local.h")
    io.writefile(external_source, '#include "local.h"\n#ifndef EXTERNAL_FILE_POLICY\n#error Per-file define lost\n#endif\nvolatile int observed_header = HEADER_VALUE;\nint main() { return EXTERNAL_FILE_POLICY == 17 && observed_header >= 13 ? 0 : 1; }\n')
    io.writefile(external_header, "#define HEADER_VALUE 13\n")
    local function quote(value) return string.format("%q", value:gsub("\\", "/")) end
    io.writefile(path.join(deep_project, "xmake.lua"), table.concat({
        'set_xmakever("3.1.1")', 'set_languages("cxx17")', 'target("external_probe")', 'set_kind("binary")',
        'set_targetdir(' .. quote(path.join(external, "bin")) .. ')',
        'add_files(' .. quote(external_source) .. ', {defines = "EXTERNAL_FILE_POLICY=17"})',
        'on_load(function(target) import("build.xmake.generated_objects", {rootdir = ' .. quote(root) .. '}).configure(target, ' .. quote(path.join(deep_build, "generated")) .. ') end)',
        'before_build(function(target)',
        'local original = path.relative(' .. quote(external_source) .. ', os.projectdir())',
        'assert(#os.projectdir() + 1 + #original >= 240)',
        'local source = target:sourcefiles()[1]',
        'assert(source == path.absolute(' .. quote(external_source) .. ') or os.host() ~= "windows")',
        'assert(table.contains(table.wrap(target:fileconfig(source).defines), "EXTERNAL_FILE_POLICY=17"))',
        'local batch = target:sourcebatches()["c++.build"]',
        'assert(batch.sourcefiles[1] == source)',
        'import("core.base.json").savefile(' .. quote(path.join(deep_build, "object.json")) .. ', {object = path.absolute(batch.objectfiles[1], os.projectdir()), source = source})',
        'end)', 'target_end()', ''}, "\n"))
    local function deep_invoke(name, args)
        local stdout, stderr = os.iorunv(os.programfile(), args, {curdir = deep_project, envs = {
            XMAKE_CONFIGDIR = path.join(directory, "external-config"), XMAKE_GLOBALDIR = envs.XMAKE_GLOBALDIR}, timeout = 120000})
        io.writefile(path.join(directory, name .. ".log"), stdout .. (stderr or ""))
    end
    deep_invoke("external-configure", {"f", "-y", "--toolchain=" .. compiler, "-P", deep_project, "-o", deep_build, "-m", "debug"})
    deep_invoke("external-cold", {"-P", deep_project, "-b", "-j", "1"})
    local external_object = import("core.base.json").loadfile(path.join(deep_build, "object.json")).object
    local external_state = {mtime = os.mtime(external_object), hash = hash.sha256(external_object)}
    deep_invoke("external-warm", {"-P", deep_project, "-b", "-j", "1"})
    assert(os.mtime(external_object) == external_state.mtime and hash.sha256(external_object) == external_state.hash)
    deep_invoke("external-run", {"run", "-P", deep_project, "external_probe"})
    os.sleep(1100)
    io.writefile(external_header, "#define HEADER_VALUE 14\n")
    deep_invoke("external-header-change", {"-P", deep_project, "-b", "-j", "1"})
    assert(hash.sha256(external_object) ~= external_state.hash, "Quoted external header change must rebuild its source object")
    deep_invoke("external-run-changed", {"run", "-P", deep_project, "external_probe"})
    import("core.base.json").savefile(path.join(directory, "receipt.json"), {
        passed = true, build_backend = "xmake", host = os.host(), compiler = compiler,
        checks = 25, compiled_translation_units = object_count, objects = snapshots,
        external_source_absolute = os.host() == "windows", external_perfile_define_preserved = true,
        external_warm_unchanged = true, external_quoted_header_rebuilt = true,
        release_objects = release_objects, same_builddir_modes_distinct = true,
        release_warm_objects_unchanged = true, debug_compact_objects_preserved = true,
        same_basename_generated_units = 2, warm_objects_unchanged = true,
        ordinary_object_path_preserved = true, deep_ordinary_compiled = true
    })
    print("NATIVE_GENERATED_OBJECT_CHECKS=25")
end
