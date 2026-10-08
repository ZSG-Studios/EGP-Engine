-- Verify installation bytes, language selection and user-file preservation.
function main()
    local root = os.curdir()
    local installer = import("misc.scripts.install_egp_net_helpers", {rootdir = root})
    local base = path.join(root, ".build/helper installer contract")
    local checks = 0
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local function rejects(project, languages)
        local failed = false
        try {function () installer.install(project, languages, root) end,
            catch {function () failed = true end}}
        return failed
    end
    for index, languages in ipairs({{"gdscript"}, {"csharp"}, {"cpp"}, {"gdscript", "csharp", "cpp"}}) do
        local project = path.join(base, tostring(index))
        os.mkdir(project)
        io.writefile(path.join(project, "project.godot"), "config_version=5\n")
        local result = installer.install(project, languages, root)
        local count = 0
        for _, group in ipairs({{"gdscript", "*.gd", ""}, {"csharp", "*.cs", "csharp"}, {"cpp", "*.hpp", "cpp"}}) do
            local selected = group[1] == "gdscript" or table.contains(languages, group[1])
            for _, source in ipairs(os.files(path.join(root, "modules/egp_net", group[1], group[2]))) do
                local target = path.join(result.destination, group[3], path.filename(source))
                check(os.isfile(target) == selected, "Language selection must control installed files")
                if selected then
                    check(hash.sha256(source) == hash.sha256(target), "Installed helper must be byte-identical")
                    count = count + 1
                end
            end
        end
        check(result.files == count, "Installation result must count actual files")
    end
    local project = path.join(base, "backup")
    os.mkdir(project)
    io.writefile(path.join(project, "project.godot"), "config_version=5\n")
    local result = installer.install(project, nil, root)
    local target = path.join(result.destination, "egp_net.gd")
    io.writefile(target, "user customization\n")
    installer.install(project, nil, root)
    check(io.readfile(target .. ".previous") == "user customization\n", "Modified helper must be backed up")
    check(hash.sha256(target) == hash.sha256(path.join(root, "modules/egp_net/gdscript/egp_net.gd")), "Backup must precede replacement")
    installer.install(project, nil, root)
    check(io.readfile(target .. ".previous") == "user customization\n", "Unchanged reinstall must preserve previous backup")
    local invalid = path.join(base, "invalid")
    os.mkdir(invalid)
    check(rejects(invalid, nil) and not os.isdir(path.join(invalid, "addons")), "Invalid project must fail before writes")
    check(rejects(project, {"unknown"}), "Unknown language must fail")
    check(rejects(project, {}), "Empty language selection must fail")
    local cli = installer.main("--project", project, "--languages", "gdscript", "cpp")
    check(cli.project == path.absolute(project), "CLI must preserve a project path containing spaces")
    print("NATIVE_HELPER_INSTALL_CHECKS=" .. checks)
end
