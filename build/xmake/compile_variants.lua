-- Xmake keys per-file policies by source pathname. Distinct policies need
-- distinct translation units even when they include the same original source.
local bytes=import('core.base.bytes')

local function canonical(value)
    local kind=type(value)
    if kind~='table' then
        assert(kind=='string' or kind=='number' or kind=='boolean' or kind=='nil','Unsupported compile policy value')
        local text=tostring(value)
        return kind .. ':' .. #text .. ':' .. text
    end
    local entries={}
    for key,item in pairs(value) do table.insert(entries,canonical(key) .. canonical(item)) end
    table.sort(entries)
    return 'table:' .. #entries .. ':' .. table.concat(entries)
end

function file(target, source, filename, generated)
    local original=path.absolute(filename,os.projectdir()):gsub('\\','/')
    local key=os.host()=='windows' and original:lower() or original
    local signature=hash.sha256(bytes(canonical(source.policy or {})))
    local registry=target:data('egp.compile_variants') or {}
    target:data_set('egp.compile_variants',registry)
    local variants=registry[key]
    if not variants then
        registry[key]={[signature]=filename}
        return filename
    end
    if variants[signature] then return variants[signature] end
    local extension=path.extension(original)
    assert(table.contains({'.c','.C','.cpp','.CPP','.cc','.cxx','.m','.mm'},extension),
        'Distinct compile policies for this source language are unsupported: ' .. original)
    assert(not original:find('["\r\n]'),'Compile variant source path cannot contain include control characters')
    local identity=target:fullname() .. '\n' .. key .. '\n' .. signature
    local directory=path.join(path.absolute(generated,os.projectdir()),'compile-variants',target:name())
    local wrapper=path.join(directory,hash.sha256(bytes(identity)) .. extension)
    local identities=target:data('egp.compile_variant_identities') or {}
    assert(not identities[wrapper] or identities[wrapper]==identity,'Compile variant filename hash collision')
    identities[wrapper]=identity
    target:data_set('egp.compile_variant_identities',identities)
    local text='// Generated compile-policy variant. Do not edit.\n#include "' .. original .. '"\n'
    os.mkdir(directory)
    if not os.isfile(wrapper) or io.readfile(wrapper)~=text then io.writefile(wrapper,text) end
    variants[signature]=wrapper
    return wrapper
end
