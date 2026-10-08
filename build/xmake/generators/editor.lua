local util = import("support", {rootdir = os.scriptdir()})

function generate(name, job, context)
    local text, src = {}, job.sources
    if name == "doc_data_class_path_builder" then
        local paths = util.read(src[1], context)
        local keys = util.sortedkeys(paths)
        table.insert(text, 'struct _DocDataClassPath {\n\tconst char *name;\n\tconst char *path;\n};\ninline constexpr int _doc_data_class_path_count = ' .. #keys .. ';\ninline constexpr _DocDataClassPath _doc_data_class_paths[' .. (#keys + 1) .. '] = {\n')
        for _, key in ipairs(keys) do table.insert(text, '\t{' .. util.cstring(key) .. ', ' .. util.cstring(paths[key]) .. '},\n') end
        table.insert(text, '\t{nullptr, nullptr},\n};\n')
    elseif name == "register_exporters_builder" or name == "register_platform_apis_builder" then
        local platforms = util.read(src[1], context)
        local exporters = name == 'register_exporters_builder'
        local family = exporters and 'exporters' or 'platform_apis'
        table.insert(text, '#include "register_' .. family .. '.h"\n')
        for _, platform in ipairs(platforms) do table.insert(text, '#include "' .. (exporters and 'platform/' or '') .. platform .. (exporters and '/export/export.h' or '/api/api.h') .. '"\n') end
        for _, item in ipairs(exporters and {{'register_exporters','register_','_exporter'}, {'register_exporter_types','register_','_exporter_types'}} or {{'register_platform_apis','register_','_api'}, {'unregister_platform_apis','unregister_','_api'}}) do
            table.insert(text, 'void ' .. item[1] .. '() {\n')
            for _, platform in ipairs(platforms) do table.insert(text, '\t' .. item[2] .. platform .. item[3] .. '();\n') end
            table.insert(text, '}\n')
        end
    elseif name == "make_doc_header" then
        local parts = {}
        for _, source in ipairs(src) do table.insert(parts, util.read(source, context)) end
        local raw = table.concat(parts)
        local compressed = util.compress(raw, context)
        -- Stable content digest replaces the process-random Python hash seed.
        import('core.base.bytes')
        table.insert(text, 'inline constexpr const char *_doc_data_hash = "' .. hash.sha256(bytes(compressed)) .. '";\ninline constexpr int _doc_data_compressed_size = ' .. #compressed .. ';\ninline constexpr int _doc_data_uncompressed_size = ' .. #raw .. ';\ninline constexpr const unsigned char _doc_data_compressed[] = {\n\t' .. util.buffer(compressed) .. '\n};\n')
    elseif name == "make_translations" then
        local header, cpp = job.targets[1], job.targets[2]
        local category = path.filename(header):match('^([^_]+)')
        local sources = table.clone(src)
        table.sort(sources, function(a,b) return path.basename(a.path) < path.basename(b.path) end)
        local rows = {}
        import('lib.detect.find_tool')
        local msgfmt = find_tool('msgfmt')
        for _, source in ipairs(sources) do
            local language, raw = path.basename(source.path), util.read(source, context)
            if msgfmt and language ~= category then
                local output = os.tmpfile() .. '.mo'
                -- A failed installed msgfmt is a genuine invalid translation, not silent fallback.
                os.vrunv(msgfmt.program, {util.sourcepath(source, context), '--no-hash', '-o', output})
                raw = assert(io.readfile(output, {encoding='binary'}))
                os.tryrm(output)
            end
            if language == category then language = 'source' end
            local compressed = util.compress(raw, context)
            local symbol = '_' .. category .. '_translation_' .. language .. '_compressed'
            table.insert(text, 'inline constexpr const unsigned char ' .. symbol .. '[] = {\n\t' .. util.buffer(compressed) .. '\n};\n')
            table.insert(rows, '\t{ ' .. util.cstring(language) .. ', ' .. #compressed .. ', ' .. #raw .. ', ' .. symbol .. ' },\n')
        end
        table.insert(text, '#include "' .. path.filename(header) .. '"\nconst EditorTranslationList _' .. category .. '_translations[] = {\n' .. table.concat(rows) .. '\t{ nullptr, 0, 0, nullptr },\n};\n')
        util.write(cpp, table.concat(text))
        util.write(header, '#ifndef EDITOR_TRANSLATION_LIST\n#define EDITOR_TRANSLATION_LIST\nstruct EditorTranslationList {\n\tconst char *lang;\n\tint comp_size;\n\tint uncomp_size;\n\tconst unsigned char *data;\n};\n#endif\nextern const EditorTranslationList _' .. category .. '_translations[];\n')
        return true
    elseif name == "force_link_builder" then
        local names, calls = {}, {}
        for _, source in ipairs(util.read(src[1], context)) do
            local symbol = path.basename(source)
            table.insert(names, '\tTEST_DLL_PRIVATE void force_link_' .. symbol .. '();\n')
            table.insert(calls, '\tforce_link_' .. symbol .. '();\n')
        end
        table.insert(text, '#ifndef _WIN32\n#define TEST_DLL_PRIVATE __attribute__((visibility("hidden")))\n#else\n#define TEST_DLL_PRIVATE\n#endif\nnamespace ForceLink {\n\tTEST_DLL_PRIVATE void force_link_tests();\n' .. table.concat(names) .. '}\nvoid ForceLink::force_link_tests() {\n' .. table.concat(calls) .. '}\n')
    else return false end
    util.write(job.targets[1], table.concat(text))
    return true
end
