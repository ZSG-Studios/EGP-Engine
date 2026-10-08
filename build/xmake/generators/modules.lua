local util = import("support", {rootdir = os.scriptdir()})

function generate(name, job, context)
    local text = {}
    if name == "modules_enabled_builder" then
        local modules = util.read(job.sources[1], context)
        local keys = #modules > 0 and table.clone(modules) or util.sortedkeys(modules)
        table.sort(keys)
        for _, module in ipairs(keys) do table.insert(text, '#define MODULE_' .. module:upper() .. '_ENABLED\n') end
    elseif name == "register_module_types_builder" then
        local modules = util.read(job.sources[1], context)
        local keys = job.module_order or util.sortedkeys(modules)
        table.insert(text, '#include "register_module_types.h"\n#include "modules/modules_enabled.gen.h"\n// IWYU pragma: begin_keep.\n')
        for _, key in ipairs(keys) do table.insert(text, '#include "' .. modules[key] .. '/register_types.h"\n') end
        table.insert(text, '// IWYU pragma: end_keep.\n')
        for _, prefix in ipairs({'initialize','uninitialize'}) do
            table.insert(text, 'void ' .. prefix .. '_modules(ModuleInitializationLevel p_level) {\n')
            for _, key in ipairs(keys) do table.insert(text, '#ifdef MODULE_' .. key:upper() .. '_ENABLED\n\t' .. prefix .. '_' .. key .. '_module(p_level);\n#endif\n') end
            table.insert(text, '}\n')
        end
    elseif name == "modules_tests_builder" then
        local headers = {}
        for _, source in ipairs(job.sources) do table.insert(headers, (path.relative(util.sourcepath(source, context), context.root):gsub('\\','/'))) end
        table.sort(headers)
        table.insert(text, '// IWYU pragma: begin_keep.\n')
        for _, header in ipairs(headers) do table.insert(text, '#include "' .. header .. '"\n') end
        table.insert(text, '// IWYU pragma: end_keep.\n')
    elseif name == "make_icu_data" then
        local value = util.read(job.sources[1], context)
        table.insert(text, '/* (C) 2016 and later: Unicode, Inc. and others. */\n/* License & terms of use: https://www.unicode.org/copyright.html */\n#include <unicode/utypes.h>\n#include <unicode/udata.h>\n#include <unicode/uversion.h>\nextern "C" U_EXPORT const size_t U_ICUDATA_SIZE = ' .. #value .. ';\nextern "C" U_EXPORT const unsigned char U_ICUDATA_ENTRY_POINT[] = {\n\t' .. util.buffer(value) .. '\n};\n')
    else return false end
    util.write(job.targets[1], table.concat(text))
    return true
end
