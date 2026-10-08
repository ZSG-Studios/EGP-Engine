-- Serializable link references; compiler-specific escaping belongs to xmake.
function references(values, root)
    local result={}
    local function visit(value)
        if type(value)=='table' and value.library then
            table.insert(result,{kind='target',name=value.library})
        elseif type(value)=='table' and value.path then
            local filename=type(value.path)=='table' and value.path.path or value.path
            assert(type(filename)=='string','Invalid native library path')
            table.insert(result,{kind='path',path=path.absolute(filename,root)})
        elseif type(value)=='table' then
            for _, item in ipairs(value) do visit(item) end
        elseif type(value)=='string' then
            assert(#value>0 and not value:find('[\r\n%z]'),'Invalid native library name')
            if value:sub(1,1)=='#' then table.insert(result,{kind='path',path=path.join(root,value:sub(2))})
            elseif path.is_absolute(value) or value:find('[/\\]') then table.insert(result,{kind='path',path=path.absolute(value,root)})
            else table.insert(result,{kind='system',name=value:gsub('^%-l','')}) end
        else raise('Unsupported native library reference: %s',tostring(value)) end
    end
    for _, value in ipairs(values or {}) do visit(value) end
    return result
end

function apply(target, policy, options)
    local seen={}
    for _, value in ipairs(policy.LIBS or {}) do
        if value.kind~='target' then
            local name=value.kind=='path' and value.path or value.name
            if not seen[name] then target:add('syslinks',name); seen[name]=true end
        end
    end
end

function flags(target, policy)
    local values, index = policy.LINKFLAGS or {}, 1
    while index <= #values do
        local flag = values[index]
        local grouped = flag == '--js-library' or flag == '--pre-js' or flag == '--post-js'
        if grouped then
            local operand = values[index + 1]
            assert(type(operand) == 'string' and operand ~= '', 'Missing Emscripten operand for ' .. flag)
            -- Xmake deduplicates flags before splitting them into argv. Keep the option
            -- and its path atomic so repeated options and quoted paths retain their meaning.
            flag = {flag, operand}
            index = index + 1
        end
        target:add('ldflags',flag,{force=true,expand=not grouped})
        target:add('shflags',flag,{force=true,expand=not grouped})
        index = index + 1
    end
end

function archive_group(target, policy, options)
    if not table.contains({'linuxbsd','android','web'},options.platform) then return end
    local libraries,seen={},{}
    for _, value in ipairs(policy.LIBS or {}) do
        if value.kind=='target' and not seen[value.name] then
            table.insert(libraries,'egp_archive_' .. value.name); seen[value.name]=true
        end
    end
    if #libraries>0 then
        target:add('linkgroups',table.unpack(table.join(libraries,{{group=true,name='egp_native_archives'}})))
        -- System shared libraries must follow their archive consumers under --as-needed.
        for _, key in ipairs({'links','syslinks'}) do
            for _, name in ipairs(target.get and target:get(key) or {}) do
                if not table.contains(libraries,name) then target:add('linkorders','linkgroup::egp_native_archives',name) end
            end
        end
    end
end
