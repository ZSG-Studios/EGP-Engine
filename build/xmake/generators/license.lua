local util = import("support", {rootdir = os.scriptdir()})

function generate(name, job, context)
    if name ~= 'make_license_header' then return false end
    local paragraphs, current, active = {}, {}, nil
    for line in util.lines(util.read(job.sources[1], context)) do
        if line:sub(1,1) ~= '#' then
            if #line == 0 then
                if not table.empty(current) then table.insert(paragraphs, current) end
                current, active = {}, nil
            elseif line:sub(1,1) == ' ' and active then table.insert(current[active], line:trim())
            else
                local key, value = line:match('^([^:]+):%s*(.*)$')
                active = key
                if key then current[key] = {value:trim()} end
            end
        end
    end
    if not table.empty(current) then table.insert(paragraphs, current) end
    local projects, order, licenses, data, parts, indexes = {}, {}, {}, {}, {}, {}
    for _, paragraph in ipairs(paragraphs) do
        if paragraph.License and not paragraph.Files then table.insert(licenses, paragraph.License)
        elseif paragraph.Comment and paragraph.Files and paragraph.Copyright and paragraph.License then
            local project = paragraph.Comment[1]
            if not projects[project] then projects[project] = {}; table.insert(order, project) end
            table.insert(projects[project], paragraph)
        end
    end
    local text = {'inline constexpr const char *GODOT_LICENSE_TEXT = {\n' .. util.rawstring(util.read(job.sources[2], context)) .. '\n};\nstruct ComponentCopyrightPart {\n\tconst char *license;\n\tconst char *const *files;\n\tconst char *const *copyright_statements;\n\tint file_count;\n\tint copyright_count;\n};\nstruct ComponentCopyright {\n\tconst char *name;\n\tconst ComponentCopyrightPart *parts;\n\tint part_count;\n};\n'}
    for _, project in ipairs(order) do
        indexes[project] = #parts
        for _, paragraph in ipairs(projects[project]) do
            local files = #data
            for _, value in ipairs(paragraph.Files) do table.insert(data, '\t' .. util.cstring(value) .. ',\n') end
            local copyright = #data
            for _, value in ipairs(paragraph.Copyright) do table.insert(data, '\t' .. util.cstring(value) .. ',\n') end
            table.insert(parts, '\t{ ' .. util.cstring(paragraph.License[1]) .. ', &COPYRIGHT_INFO_DATA[' .. files .. '], &COPYRIGHT_INFO_DATA[' .. copyright .. '], ' .. #paragraph.Files .. ', ' .. #paragraph.Copyright .. ' },\n')
        end
    end
    table.insert(text, 'inline constexpr const char *COPYRIGHT_INFO_DATA[] = {\n' .. table.concat(data) .. '};\ninline constexpr ComponentCopyrightPart COPYRIGHT_PROJECT_PARTS[] = {\n' .. table.concat(parts) .. '};\ninline constexpr int COPYRIGHT_INFO_COUNT = ' .. #order .. ';\ninline constexpr ComponentCopyright COPYRIGHT_INFO[] = {\n')
    for _, project in ipairs(order) do table.insert(text, '\t{ ' .. util.cstring(project) .. ', &COPYRIGHT_PROJECT_PARTS[' .. indexes[project] .. '], ' .. #projects[project] .. ' },\n') end
    table.insert(text, '};\ninline constexpr int LICENSE_COUNT = ' .. #licenses .. ';\ninline constexpr const char *LICENSE_NAMES[] = {\n')
    for _, license in ipairs(licenses) do table.insert(text, '\t' .. util.cstring(license[1]) .. ',\n') end
    table.insert(text, '};\ninline constexpr const char *LICENSE_BODIES[] = {\n')
    for _, license in ipairs(licenses) do
        local lines = {}
        for index = 2, #license do table.insert(lines, license[index] == '.' and '' or license[index]) end
        table.insert(text, '(' .. util.rawstring(table.concat(lines, '\n') .. '\n') .. '),\n')
    end
    table.insert(text, '};\n')
    util.write(job.targets[1], table.concat(text))
    return true
end
