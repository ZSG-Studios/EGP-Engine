local util = import("support", {rootdir = os.scriptdir()})

function generate(name, job, context)
    local text, src = {}, job.sources
    if name == "areatex_builder" or name == "searchtex_builder" then
        local area = name == "areatex_builder"
        local prefix = area and "AREATEX" or "SEARCHTEX"
        table.insert(text, '#define ' .. prefix .. '_WIDTH ' .. (area and '160' or '64') .. '\n#define ' .. prefix .. '_HEIGHT ' .. (area and '560' or '16') .. '\n#define ' .. prefix .. '_PITCH ' .. (area and '(AREATEX_WIDTH * 2)' or 'SEARCHTEX_WIDTH') .. '\n#define ' .. prefix .. '_SIZE (' .. prefix .. '_HEIGHT * ' .. prefix .. '_PITCH)\ninline constexpr const unsigned char ' .. (area and 'area' or 'search') .. '_tex_png[] = {\n\t' .. util.buffer(util.read(src[1], context)) .. '\n};\n')
    elseif name == "make_splash" or name == "make_splash_editor" or name == "make_app_icon" then
        local symbol = name == "make_app_icon" and "app_icon" or name == "make_splash_editor" and "boot_splash_editor" or "boot_splash"
        if name ~= "make_app_icon" then
            table.insert(text, '#include "core/math/color.h"\nstatic const Color ' .. symbol .. '_bg_color = Color(' .. (name == "make_splash_editor" and '0.125, 0.145, 0.192' or '0.14, 0.14, 0.14') .. ');\n')
        end
        table.insert(text, 'inline constexpr const unsigned char ' .. symbol .. '_png[] = {\n\t' .. util.buffer(util.read(src[1], context)) .. '\n};\n')
    elseif name == "make_fonts_header" then
        for _, source in ipairs(src) do
            local value, font = util.read(source, context), path.basename(source.path)
            table.insert(text, 'inline constexpr int _font_' .. font .. '_size = ' .. #value .. ';\ninline constexpr unsigned char _font_' .. font .. '[] = {\n\t' .. util.buffer(value) .. '\n};\n')
        end
    elseif name == "make_editor_icons_action" or name == "make_default_theme_icons_action" then
        local prefix = name == "make_editor_icons_action" and "editor" or "default_theme"
        local names, values, medium, big = {}, {}, {}, {}
        for index, source in ipairs(src) do
            local icon = path.basename(source.path)
            table.insert(names, util.cstring(icon))
            table.insert(values, util.rawstring(util.read(source, context)))
            if icon:endswith('MediumThumb') then table.insert(medium, tostring(index - 1))
            elseif icon:endswith('BigThumb') or icon:endswith('GodotFile') then table.insert(big, tostring(index - 1)) end
        end
        table.insert(text, 'inline constexpr int ' .. prefix .. '_icons_count = ' .. #names .. ';\ninline constexpr const char *' .. prefix .. '_icons_sources[] = {\n\t' .. table.concat(values, ',\n\t') .. '\n};\ninline constexpr const char *' .. prefix .. '_icons_names[] = {\n\t' .. table.concat(names, ',\n\t') .. '\n};\n')
        if prefix == 'editor' then
            table.insert(text, 'inline constexpr int editor_md_thumbs_count = ' .. #medium .. ';\ninline constexpr int editor_md_thumbs_indices[] = { ' .. table.concat(medium, ', ') .. ' };\ninline constexpr int editor_bg_thumbs_count = ' .. #big .. ';\ninline constexpr int editor_bg_thumbs_indices[] = { ' .. table.concat(big, ', ') .. ' };\n')
        end
    elseif name == "export_icon_builder" then
        local source = src[1].path
        local platform = path.filename(path.directory(path.directory(source)))
        table.insert(text, 'inline constexpr const char *_' .. platform .. '_' .. path.basename(source) .. '_svg = ' .. util.rawstring(util.read(src[1], context)) .. ';\n')
    elseif name == "make_templates" then
        local templates = {}
        local delimiter = #src > 0 and path.extension(src[1].path) == '.cs' and '//' or '#'
        for _, source in ipairs(src) do
            local meta = {name='',description='',version='', ['space-indent']='4'}
            local script = {}
            for line in util.lines(util.read(source, context)) do
                local marker = delimiter .. ' meta-'
                if line:startswith(marker) then
                    local key, value = line:sub(#marker + 1):match('^([%w%-]+):?%s*(.*)$')
                    if meta[key] ~= nil then meta[key] = value:trim() end
                else table.insert(script, line .. '\n') end
            end
            if #meta.name == 0 then meta.name = path.basename(source.path):gsub('_',' '):gsub('(%a)([%w]*)',function(a,b) return a:upper() .. b:lower() end) end
            local code = table.concat(script):gsub('^%s+', '')
            if #meta['space-indent'] > 0 then
                local count = tonumber(meta['space-indent'])
                assert(count and count > 0, 'Script template indentation must be positive')
                code = code:gsub(string.rep(' ', count), '_TS_')
            end
            code = code:gsub('\t','_TS_')
            table.insert(templates, '{ String(' .. util.cstring(path.filename(path.directory(source.path))) .. '), String(' .. util.cstring(meta.name) .. '), String(' .. util.cstring(meta.description) .. '), String(' .. util.cstring(code) .. ') },')
        end
        table.insert(text, '#include "core/object/script_language.h"\n#include "core/string/ustring.h"\ninline constexpr int TEMPLATES_ARRAY_SIZE = ' .. #templates .. ';\nstatic const struct ScriptLanguage::ScriptTemplate TEMPLATES[TEMPLATES_ARRAY_SIZE] = {\n\t' .. table.concat(templates, '\n\t') .. '\n};\n')
    else return false end
    util.write(job.targets[1], table.concat(text))
    return true
end
