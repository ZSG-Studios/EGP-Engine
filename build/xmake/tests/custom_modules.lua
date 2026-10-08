-- External native recipes must be discovered, configured and aggregated without another backend.
function main()
    local root = os.projectdir()
    local model = import("build.xmake.graph", {rootdir = root})
    local base = path.join(root, "build/xmake/tests/fixtures/custom_modules")
    local checks = 0
    local function check(value) assert(value); checks = checks + 1 end
    local function graph(extra)
        local options = {platform = "windows", arch = "x86_64", target = "template_debug", custom_modules = base}
        for name, value in pairs(extra or {}) do options[name] = value end
        local result = model.new(root, options)
        result:configure()
        return result
    end
    local baseline = graph()
    check(baseline.options.qualification_feature == false)
    check(baseline.custom_names.qualification_nested == true and baseline.options.module_qualification_nested_enabled == false)
    local shallow = graph({custom_modules_recursive = false})
    check(shallow.custom_names.qualification_nested == nil and shallow.custom_names.qualification_external == true)
    check(baseline.options.module_qualification_external_enabled == true)
    local configured = graph({custom_module_options = '{"qualification_feature":"y"}'})
    check(configured.options.qualification_feature == true)
    check(configured.custom_folders.qualification_external == path.join(base, "qualification_external"))
    check(configured.environment.doc_class_path.QualificationExternal:endswith("qualification_external/doc_classes"))
    local archive
    for _, library in ipairs(configured.libraries) do
        if library.name == "custom_modules" then archive = library end
        check(library.name ~= "external_vendor_archive" and library.name ~= "module_qualification_external")
    end
    check(archive and #archive.sources == 2)
    for _, source in ipairs(archive.sources) do check(table.contains(source.environment.CPPDEFINES, "QUALIFICATION_FEATURE")) end
    local disabled = graph({custom_module_options = '{"module_qualification_external_enabled":"n"}'})
    check(disabled.environment.module_list.qualification_external == nil)
    local dependency = graph({module_box3d_enabled = false})
    check(dependency.environment.module_list.qualification_external == nil and dependency.options.module_qualification_external_enabled == false)
    local unknown = utils.trycall(function() graph({custom_module_options = '{"undeclared_feature":true}'}) end)
    check(not unknown)
    local missing = utils.trycall(function() graph({custom_modules = path.join(base, "absent")}) end)
    check(not missing)
    print("NATIVE_CUSTOM_MODULE_CHECKS=" .. checks)
end
