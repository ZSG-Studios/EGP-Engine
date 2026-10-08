set_xmakever("3.1.1")
set_languages("cxx17")
add_rules("mode.debug", "mode.release")
target("egp_generated_object_probe")
    set_kind("binary")
    add_files("ordinary.cpp", "modules_openxr/extensions/spatial_container/openxr_spatial_container_self_rendering_extension.cpp")
    on_load(function(target)
        local config = import("core.project.config")
        local root = path.absolute("../../../..", os.projectdir())
        local generated = path.join(path.absolute(config.builddir(), os.projectdir()), "generated")
        target:set("objectdir", path.join(path.absolute(config.builddir(), os.projectdir()), "objects", target:name()))
        for index, name in ipairs({"first", "second"}) do
            local source = path.join(generated, name, "register_module_types.gen.cpp")
            os.mkdir(path.directory(source))
            local text = "int " .. name .. "_generated() { return " .. (index == 1 and "19" or "23") .. "; }\n"
            if not os.isfile(source) or io.readfile(source) ~= text then io.writefile(source, text) end
            target:add("files", source)
        end
        target:set("config", function(current)
            local expected = {}
            for _, batch in pairs(current:sourcebatches()) do
                for index, source in ipairs(batch.objectfiles and batch.sourcefiles or {}) do
                    local object, dependency = batch.objectfiles[index], batch.dependfiles[index]
                    local relative = path.relative(path.absolute(source, os.projectdir()), generated):gsub("\\", "/")
                    local is_generated = relative ~= ".." and not relative:startswith("../") and not path.is_absolute(relative)
                    local too_long = os.host() == "windows" and
                        (#path.absolute(object, os.projectdir()) >= 240 or
                        (not path.is_absolute(object) and #os.projectdir() + 1 + #object >= 240) or
                        #path.absolute(dependency, os.projectdir()) >= 240 or
                        (not path.is_absolute(dependency) and #os.projectdir() + 1 + #dependency >= 240))
                    expected[path.filename(source)] = {object = object, dependency = dependency, compact = is_generated or too_long}
                    if is_generated then expected[path.filename(source) .. path.directory(source)] = expected[path.filename(source)] end
                end
            end
            current:data_set("probe.originals", expected)
        end)
        import("build.xmake.generated_objects", {rootdir = root}).configure(target, generated)
    end)
    before_build(function(target)
        local mapping = assert(target:data("egp.generated_objects.mapping"))
        local count, seen, ordinary, deep = 0, {}, false, false
        for original, object in pairs(mapping) do
            assert(original ~= object and not seen[object])
            assert(#path.absolute(object) < 240)
            seen[object], count = true, count + 1
        end
        local expected_count = 0
        for _, batch in pairs(target:sourcebatches()) do
            for index, source in ipairs(batch.objectfiles and batch.sourcefiles or {}) do
                local object = batch.objectfiles[index]
                local originals = target:data("probe.originals")
                local original = originals[path.filename(source) .. path.directory(source)] or originals[path.filename(source)]
                assert(original)
                assert((mapping[original.object] ~= nil) == original.compact)
                assert(object == (mapping[original.object] or original.object))
                if original.compact then expected_count = expected_count + 1 end
                assert(table.contains(target:objectfiles(), object))
                assert(batch.dependfiles[index] == (seen[object] and object .. ".d" or target:dependfile(object)))
                if path.filename(source) == "openxr_spatial_container_self_rendering_extension.cpp" then
                    assert(#path.absolute(target:objectfile(source), os.projectdir()) > 260)
                    if os.host() == "windows" then
                        assert(object ~= target:objectfile(source) and #path.absolute(object .. ".d") < 240)
                    else assert(object == target:objectfile(source)) end
                    deep = true
                end
                if path.filename(source) == "ordinary.cpp" then
                    ordinary = true
                end
            end
        end
        assert(ordinary and deep)
        assert(count == expected_count and count >= 2)
        local config = import("core.project.config")
        import("core.base.json").savefile(path.join(path.absolute(config.builddir(), os.projectdir()), "linked-objects.json"), target:objectfiles())
        import("core.base.json").savefile(path.join(path.absolute(config.builddir(), os.projectdir()), "mapping-count.json"), {count = count})
        print("NATIVE_GENERATED_OBJECT_GRAPH_CHECKS=11")
    end)
target_end()

target("short_probe")
    set_kind("static")
    add_files("ordinary.cpp")
    on_load(function(target)
        local root = path.absolute("../../../..", os.projectdir())
        target:set("objectdir", path.join(root, ".build/short-objects"))
        target:set("dependir", path.join(root, ".build/short-deps"))
        target:set("config", function(current)
            current:objectfiles()
            local batch = current:sourcebatches()["c++.build"]
            current:data_set("probe.original_object", batch.objectfiles[1])
            current:data_set("probe.original_dependency", batch.dependfiles[1])
        end)
        import("build.xmake.generated_objects", {rootdir = root}).configure(target, path.join(root, ".build/nonexistent-generated"))
    end)
    before_build(function(target)
        local count = 0
        for _ in pairs(target:data("egp.generated_objects.mapping")) do count = count + 1 end
        assert(count == 0)
        local batch = target:sourcebatches()["c++.build"]
        local object, dependency = target:data("probe.original_object"), target:data("probe.original_dependency")
        assert(#path.absolute(object, os.projectdir()) < 240 and #path.absolute(dependency, os.projectdir()) < 240)
        assert(batch.objectfiles[1] == object and batch.dependfiles[1] == dependency)
        assert(target:objectfiles()[1] == object)
        local config = import("core.project.config")
        import("core.base.json").savefile(path.join(path.absolute(config.builddir(), os.projectdir()), "short-object.json"), {object = path.absolute(object, os.projectdir())})
    end)
target_end()
