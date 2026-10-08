local util = import('support', {rootdir=os.scriptdir()})
local templates = import('virtuals_templates', {rootdir=os.scriptdir()})
local function replace(value, old, new)
    return (value:gsub(old:gsub('(%W)','%%%1'),function() return new end))
end

local function version(count, constant, returns, required, compat)
    local s = templates.proto()
    local function set(old,new) s=replace(s,old,new) end
    set('$SCRIPTCALL',compat and '' or templates.script_call())
    set('$SCRIPTHASMETHOD',compat and '' or templates.script_has_method())
    local name=tostring(count)
    local info,flags='','METHOD_FLAG_VIRTUAL'
    if returns then
        name=name .. 'R'; set('$RET','m_ret,'); set('$RVOID','(void)r_ret;'); set('$CALLPTRRETDEF','PtrToArg<m_ret>::EncodeT ret;')
        info='method_info.return_val = GetTypeInfo<m_ret>::get_class_info();\\\n\t\tmethod_info.return_val_metadata = GetTypeInfo<m_ret>::METADATA;'
    else
        set('$RET ',''); set('\t\t$RVOID\\\n',''); set('\t\t\t$CALLPTRRETDEF\\\n','')
    end
    if constant then name=name .. 'C'; flags=flags .. ' | METHOD_FLAG_CONST'; set('$CONST','const') else set('$CONST ','') end
    if required then
        name=name .. '_REQUIRED'; flags=flags .. ' | METHOD_FLAG_VIRTUAL_REQUIRED'
        set('$REQCHECK','static bool _gdvirtual_required_error_shown = false;\\\n\t\t_gdvirtual_print_required_error(this, _gdvirtual_##m_name##_sn, _gdvirtual_required_error_shown);')
    else set('\t\t$REQCHECK\\\n','') end
    if compat then name=name .. '_COMPAT'; set('$COMPAT','true'); set('$ALIAS','m_alias,'); set('$VARNAME','m_alias')
    else set('$COMPAT','false'); set('$ALIAS ',''); set('$VARNAME','m_name') end
    set('$METHOD_FLAGS',flags); set('$VER',name)
    local types, params, variants, vptrs, pointers, calls = {},{},{},{},{},{}
    for index=1,count do
        table.insert(types,'m_type' .. index)
        table.insert(params,'m_type' .. index .. ' arg' .. index)
        table.insert(variants,'VariantInternal::make(arg' .. index .. ')')
        table.insert(vptrs,'&vargs[' .. (index-1) .. ']')
        table.insert(pointers,'&argval' .. index)
        table.insert(calls,(index>1 and '\t\t\t' or '') .. 'PtrToArg<m_type' .. index .. '>::EncodeT argval' .. index .. '; PtrToArg<m_type' .. index .. '>::encode(arg' .. index .. ', &argval' .. index .. ');\\\n')
    end
    if count>0 then
        if #info>0 then info=info .. '\\\n\t\t' end
        info=info .. '_gdvirtual_set_method_info_args<' .. table.concat(types,', ') .. '>(method_info);'
        set('$CALLSIARGS','Variant vargs[' .. count .. '] = { ' .. table.concat(variants,', ') .. ' };\\\n\t\t\tconst Variant *vargptrs[' .. count .. '] = { ' .. table.concat(vptrs,', ') .. ' };')
        set('$CALLSIARGPASS','(const Variant **)vargptrs, ' .. count)
        set('$CALLPTRARGS',table.concat(calls) .. '\t\t\tGDExtensionConstTypePtr argptrs[' .. count .. '] = { ' .. table.concat(pointers,', ') .. ' };')
        set('$CALLPTRARGPASS','reinterpret_cast<GDExtensionConstTypePtr *>(argptrs)')
    else
        set('\t\t\t$CALLSIARGS\\\n',''); set('$CALLSIARGPASS','nullptr, 0')
        set('\t\t\t$CALLPTRARGS\\\n',''); set('$CALLPTRARGPASS','nullptr')
    end
    if returns then
        table.insert(params,'m_ret &r_ret')
        set('$CALLSIBEGIN','Variant ret = '); set('$CALLSIRET','r_ret = VariantCaster<m_ret>::cast(ret);')
        set('$CALLPTRRETPASS','&ret'); set('$CALLPTRRET','r_ret = (m_ret)ret;')
    else
        set('$CALLSIBEGIN',''); set('\t\t\t\t$CALLSIRET\\\n','')
        set('$CALLPTRRETPASS','nullptr'); set('\t\t\t\t$CALLPTRRET\\\n','')
    end
    set(' $ARG',#types>0 and ', ' .. table.concat(types,', ') or '')
    set('$CALLARGS',table.concat(params,', '))
    if #info>0 then set('$FILL_METHOD_INFO',info) else set('\t\t$FILL_METHOD_INFO\\\n','') end
    return s
end

function generate(job,context)
    if not job.builder:find('make_virtuals',1,true) then return false end
    local text={templates.header()}
    for count=0,12 do
        for _, mode in ipairs({'normal','required','compat'}) do
            for _, constant in ipairs({false,true}) do
                for _, returns in ipairs({false,true}) do table.insert(text,version(count,constant,returns,mode=='required',mode=='compat')) end
            end
        end
    end
    util.write(job.targets[1],table.concat(text),false)
    return true
end
