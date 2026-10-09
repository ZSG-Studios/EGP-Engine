raise("Legacy networking helper installation is retired. Use native Superpos classes and matching generated bindings.")
-- Install checked-in networking helpers into an existing game project.
function install(project, languages, root)
    project = path.absolute(assert(project, "--project is required"))
    root = root or path.absolute(path.join(os.scriptdir(), "../.."))
    assert(os.isfile(path.join(project, "project.godot")), "Destination must contain project.godot")
    languages = languages or {"gdscript"}
    assert(#languages > 0, "--languages requires at least one language")
    local selected = {gdscript = true}
    for _, language in ipairs(languages) do
        assert(table.contains({"gdscript", "csharp", "cpp"}, language), "Unknown language: " .. language)
        selected[language] = true
    end
    local destination = path.join(project, "addons/egp_net")
    local files = 0
    for _, group in ipairs({{"gdscript", "*.gd", ""}, {"csharp", "*.cs", "csharp"}, {"cpp", "*.hpp", "cpp"}}) do
        if selected[group[1]] then
            local folder = path.join(destination, group[3])
            os.mkdir(folder)
            for _, source in ipairs(os.files(path.join(root, "modules/egp_net", group[1], group[2]))) do
                local target = path.join(folder, path.filename(source))
                if os.isfile(target) and io.readfile(target, {encoding = "binary"}) ~= io.readfile(source, {encoding = "binary"}) then
                    os.cp(target, target .. ".previous")
                end
                os.cp(source, target)
                files = files + 1
            end
        end
    end
    return {project = project, destination = destination, files = files, languages = languages}
end

function main(...)
    local argv, project, languages, index = {...}, nil, nil, 1
    while index <= #argv do
        local token = argv[index]
        if token == "--project" then
            index = index + 1
            project = assert(argv[index], "--project requires a path")
        elseif token:startswith("--project=") then
            project = token:sub(11)
        elseif token == "--languages" then
            languages = {}
            while argv[index + 1] and not argv[index + 1]:startswith("--") do
                index = index + 1
                table.insert(languages, argv[index])
            end
        else
            error("Unknown option: " .. token)
        end
        index = index + 1
    end
    local result = install(project, languages)
    print("Installed EGP networking APIs (" .. table.concat(result.languages, ", ") .. ") into " .. result.destination)
    return result
end
