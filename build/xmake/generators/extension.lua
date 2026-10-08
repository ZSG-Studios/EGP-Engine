local util = import("support", {rootdir = os.scriptdir()})

local function wrappers()
    local text = {}
    for _, family in ipairs({'EXBIND','MODBIND'}) do
        for count = 0, 12 do
            for _, constant in ipairs({false,true}) do
                for _, returns in ipairs({false,true}) do
                    local version = tostring(count) .. (returns and 'R' or '') .. (constant and 'C' or '')
                    local args, params, calls = {}, {}, {}
                    for index=1,count do
                        table.insert(args,'m_type' .. index)
                        table.insert(params,'m_type' .. index .. ' arg' .. index)
                        table.insert(calls,'arg' .. index)
                    end
                    local types = #args > 0 and ', ' .. table.concat(args,', ') or ''
                    local ret, qualifier = returns and 'm_ret' or 'void', constant and 'const' or ''
                    table.insert(text, '\n#define ' .. family .. version .. '(' .. (returns and 'm_ret, ' or '') .. 'm_name' .. types .. ') \\\n')
                    if family == 'MODBIND' then
                        table.insert(text, 'virtual ' .. ret .. ' _##m_name(' .. table.concat(params,', ') .. ') ' .. qualifier .. '; \\\n_FORCE_INLINE_ virtual ' .. ret .. ' m_name(' .. table.concat(params,', ') .. ') ' .. qualifier .. ' override { \\\n    ' .. (returns and 'return ' or '') .. '_##m_name(' .. table.concat(calls,', ') .. ');\\\n}\n')
                    else
                        table.insert(text, 'GDVIRTUAL' .. version .. '_REQUIRED(' .. (returns and 'm_ret, ' or '') .. '_##m_name' .. types .. ')\\\nvirtual ' .. ret .. ' m_name(' .. table.concat(params,', ') .. ') ' .. qualifier .. ' override { \\\n')
                        if returns then table.insert(text,'    m_ret ret; ZeroInitializer<m_ret>::initialize(ret);\\\n') end
                        table.insert(text, '    GDVIRTUAL_CALL(_##m_name' .. (#calls > 0 and ', ' .. table.concat(calls,', ') or '') .. (returns and ', ret' or '') .. ');\\\n    ' .. (returns and 'return ret;' or 'return;') .. '\\\n}\n')
                    end
                end
            end
        end
    end
    return table.concat(text)
end

local function interface_header(data)
    local text = {'#ifndef __cplusplus\n#include <stddef.h>\n#include <stdint.h>\ntypedef uint32_t char32_t;\ntypedef uint16_t char16_t;\n#else\n#include <cstddef>\n#include <cstdint>\nextern "C" {\n#endif\n\n'}
    local known, interfaces, replacements = {}, {}, {}
    for _, name in ipairs({'void','int8_t','uint8_t','int16_t','uint16_t','int32_t','uint32_t','int64_t','uint64_t','size_t','char','char16_t','char32_t','wchar_t','float','double'}) do known[name] = true end
    local function typed(type, name)
        if type:endswith('*') then return type:sub(1,-2) .. ' *' .. (name or '') end
        return type .. (name and ' ' .. name or '')
    end
    local function validate(type)
        local base = type:gsub('^const ',''):gsub('%*$','')
        assert(type ~= 'void' and type ~= 'const void' and known[base], 'Unknown interface type: ' .. type)
    end
    local function documentation(lines, indent)
        indent = indent or ''
        if #lines == 1 then table.insert(text,indent .. '/* ' .. lines[1] .. ' */\n'); return end
        for index,line in ipairs(lines) do table.insert(text,indent .. (index==1 and '/*' or ' *') .. (#line>0 and ' ' .. line or '') .. '\n') end
        table.insert(text,indent .. ' */\n')
    end
    local function deprecated(value)
        if not value then return '' end
        return 'Deprecated in Godot ' .. value.since .. '.' .. (value.message and ' ' .. value.message or '') .. (value.replace_with and ' Use `' .. value.replace_with .. '` instead.' or '')
    end
    local function comment(value)
        return value.deprecated and ' /* ' .. deprecated(value.deprecated) .. ' */' or ''
    end
    local function function_type(value,name)
        local args = {}
        for _, arg in ipairs(value.arguments or {}) do validate(arg.type); table.insert(args,typed(arg.type,arg.name)) end
        local ret = value.return_value and value.return_value.type or 'void'
        if value.return_value then validate(ret) end
        table.insert(text,'typedef ' .. typed(ret,'(*' .. (name or value.name) .. ')(' .. table.concat(args,', ') .. ')') .. ';' .. (name and '' or comment(value)) .. '\n')
    end
    assert(data.types and data.interface and data.format_version, 'Incomplete GDExtension interface schema')
    for _, value in ipairs(data.types) do
        assert(value.name and not known[value.name], 'Duplicate interface type: ' .. tostring(value.name))
        if value.description then documentation(value.description) end
        if value.kind == 'handle' then
            if value.parent then assert(known[value.parent] and known[value.parent].kind=='handle','Unknown parent handle') end
            table.insert(text,'typedef ' .. typed(value.is_const and 'const void*' or 'void*',value.name) .. ';' .. comment(value) .. '\n')
        elseif value.kind == 'alias' then
            validate(value.type)
            table.insert(text,'typedef ' .. typed(value.type,value.name) .. ';' .. comment(value) .. '\n')
        elseif value.kind == 'enum' then
            table.insert(text,'typedef enum {\n')
            for _, item in ipairs(assert(value.values)) do
                if item.description then documentation(item.description,'\t') end
                local number = type(item.value)=='number' and string.format('%.0f',item.value) or tostring(item.value)
                assert(type(item.value)~='number' or item.value==math.floor(item.value),'Enum values must be integers')
                table.insert(text,'\t' .. item.name .. ' = ' .. number .. ',\n')
            end
            table.insert(text,'} ' .. value.name .. ';' .. comment(value) .. '\n\n')
        elseif value.kind == 'struct' then
            table.insert(text,'typedef struct {\n')
            for _, item in ipairs(assert(value.members)) do
                validate(item.type)
                if item.description then documentation(item.description,'\t') end
                table.insert(text,'\t' .. typed(item.type,item.name) .. ';\n')
            end
            table.insert(text,'} ' .. value.name .. ';' .. comment(value) .. '\n\n')
        elseif value.kind == 'function' then function_type(value)
        else raise('Unknown GDExtension type kind: %s',tostring(value.kind)) end
        known[value.name] = value
        if value.deprecated and value.deprecated.replace_with then table.insert(replacements,value) end
    end
    for _, value in ipairs(replacements) do
        local replacement = known[value.deprecated.replace_with]
        assert(replacement and (type(replacement) ~= 'table' or not replacement.deprecated), 'Invalid deprecated type replacement')
    end
    for _, value in ipairs(data.interface) do
        assert(not interfaces[value.name],'Duplicate interface function: ' .. value.name)
        interfaces[value.name] = value
        local docs = {'@name ' .. value.name,'@since ' .. assert(value.since)}
        if value.deprecated then table.insert(docs,'@deprecated ' .. deprecated(value.deprecated)) end
        table.insert(docs,'')
        for index,line in ipairs(assert(value.description)) do
            if index==2 then table.insert(docs,'') end
            table.insert(docs,line)
        end
        if value.arguments then
            table.insert(docs,'')
            for _, arg in ipairs(value.arguments) do table.insert(docs,'@param ' .. arg.name .. ' ' .. table.concat(assert(arg.description), ' ')) end
        end
        if value.return_value then
            table.insert(docs,''); table.insert(docs,'@return ' .. table.concat(assert(value.return_value.description),' '))
        end
        if value.see then
            table.insert(docs,''); for _, see in ipairs(value.see) do table.insert(docs,'@see ' .. see) end
        end
        table.insert(text,'/**\n')
        for _, line in ipairs(docs) do table.insert(text,' *' .. (#line > 0 and ' ' .. line or '') .. '\n') end
        table.insert(text,' */\n')
        local name = value.name:gsub('(^%l)',string.upper):gsub('_(%l)',string.upper)
        name = name:sub(1,1):upper() .. name:sub(2)
        function_type(value,'GDExtensionInterface' .. name)
        table.insert(text,'\n')
    end
    for _, value in ipairs(data.interface) do
        if value.deprecated and value.deprecated.replace_with then
            local replacement = interfaces[value.deprecated.replace_with]
            assert(replacement and not replacement.deprecated,'Invalid deprecated interface replacement')
        end
    end
    table.insert(text,'#ifdef __cplusplus\n}\n#endif\n')
    return table.concat(text)
end

function generate(job, context)
    local builder = job.builder:gsub('/','.')
    if builder:find('make_wrappers',1,true) then
        util.write(job.targets[1],wrappers())
    elseif builder:find('make_interface_header',1,true) then
        import('core.base.json')
        util.write(job.targets[1],interface_header(json.decode(util.read(job.sources[1],context))))
    elseif builder:find('make_interface_dumper',1,true) then
        local raw = util.read(job.sources[1],context)
        local compressed = util.compress(raw,context)
        local body = '#ifdef TOOLS_ENABLED\n#include "core/io/compression.h"\n#include "core/io/file_access.h"\n#include "core/string/ustring.h"\ninline constexpr int _gdextension_interface_data_compressed_size = ' .. #compressed .. ';\ninline constexpr int _gdextension_interface_data_uncompressed_size = ' .. #raw .. ';\ninline constexpr unsigned char _gdextension_interface_data_compressed[] = {\n\t' .. util.buffer(compressed) .. '\n};\n'
        body = body .. [[
class GDExtensionInterfaceDump {
public:
    static Vector<uint8_t> load_gdextension_interface_file() {
        Vector<uint8_t> data;
        data.resize(_gdextension_interface_data_uncompressed_size);
        int ret = Compression::decompress(data.ptrw(), _gdextension_interface_data_uncompressed_size, _gdextension_interface_data_compressed, _gdextension_interface_data_compressed_size, Compression::MODE_DEFLATE);
        ERR_FAIL_COND_V_MSG(ret == -1, Vector<uint8_t>(), "Compressed file is corrupt.");
        return data;
    }
    static void generate_gdextension_interface_file(const String &p_path) {
        Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::WRITE);
        ERR_FAIL_COND_MSG(fa.is_null(), vformat("Cannot open file '%s' for writing.", p_path));
        Vector<uint8_t> data = load_gdextension_interface_file();
        if (data.size() > 0) { fa->store_buffer(data.ptr(), data.size()); }
    }
};
#endif
]]
        util.write(job.targets[1],body)
    else return false end
    return true
end
