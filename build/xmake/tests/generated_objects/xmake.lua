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
        assert(count == (os.host() == "windows" and 3 or 2))
        for _, batch in pairs(target:sourcebatches()) do
            for index, source in ipairs(batch.objectfiles and batch.sourcefiles or {}) do
                local object = batch.objectfiles[index]
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
                    assert(object == target:objectfile(source))
                    ordinary = true
                end
            end
        end
        assert(ordinary and deep)
        local config = import("core.project.config")
        import("core.base.json").savefile(path.join(path.absolute(config.builddir(), os.projectdir()), "linked-objects.json"), target:objectfiles())
        print("NATIVE_GENERATED_OBJECT_GRAPH_CHECKS=11")
    end)
target_end()
