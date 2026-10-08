-- Give each editor operation a project-local working directory and config lock.
function main(project, configuration, ...)
    assert(path.is_absolute(project) and os.isfile(path.join(project, "xmake.lua")), "Choose an absolute xmake project directory")
    assert(path.is_absolute(configuration), "Choose an absolute isolated configuration directory")
    local arguments = {...}
    -- Explicit project selection prevents upward discovery from choosing the
    -- engine's root project when this SDK is extracted below that checkout.
    local selected = false
    for index, argument in ipairs(arguments) do
        if argument == "-P" then
            assert(arguments[index + 1] == project, "Conflicting project selection")
            selected = true
        end
    end
    if not selected then
        local position = arguments[1] and not arguments[1]:startswith("-") and 2 or 1
        table.insert(arguments, position, "-P")
        table.insert(arguments, position + 1, project)
    end
    os.execv(os.programfile(), arguments, {curdir = project, envs = {
        XMAKE_CONFIGDIR = configuration,
        XMAKE_GLOBALDIR = path.join(configuration, "global")
    }})
end
