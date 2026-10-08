-- Cache receipts must authenticate their generated data, not merely its existence.
function signature(bootstrap, implementations, sources, root)
    local bytes=import('core.base.bytes')
    local json=import('core.base.json')
    local entries={}
    local function add(kind,filename)
        local absolute=path.absolute(filename,root)
        local name=path.relative(absolute,root):gsub('\\','/')
        table.insert(entries,json.encode({kind,name,hash.sha256(absolute)}))
    end
    for _, filename in ipairs(implementations) do add('implementation',filename) end
    for _, source in ipairs(sources) do
        if source.path then add('sdk',source.path)
        else table.insert(entries,json.encode({'literal',source.value})) end
    end
    table.sort(entries)
    return hash.sha256(bytes(json.encode({hash.sha256(path.absolute(bootstrap,root)),entries})))
end

function valid(previous, signature, api, header)
    return previous.signature==signature and os.isfile(api) and os.isfile(header)
        and previous.api_hash==hash.sha256(api) and previous.header_hash==hash.sha256(header)
end

function dump_fresh(bootstrap, directory, runner)
    local json=import('core.base.json')
    local capture=path.join(directory,'bootstrap-capture')
    local output=path.join(capture,'extension_api.json')
    os.mkdir(capture)
    assert(not os.isdir(output),'API capture output must be a file')
    if os.isfile(output) then os.rm(output) end
    (runner or os.vrunv)(bootstrap,{'--headless','--dump-extension-api'},{curdir=capture,timeout=120000})
    assert(os.isfile(output),'Editor bootstrap exited without publishing a fresh extension API')
    local api=json.loadfile(output)
    assert(type(api)=='table' and type(api.header)=='table' and type(api.classes)=='table' and #api.classes>0,'Fresh extension API has an invalid header/classes structure')
    for _, key in ipairs({'version_major','version_minor','version_patch'}) do
        local value=api.header[key]
        assert(type(value)=='number' and value==math.floor(value) and value>=0 and (key~='version_major' or value>0),'Invalid extension API version header')
    end
    for _, key in ipairs({'version_status','version_build','version_full_name'}) do assert(type(api.header[key])=='string' and #api.header[key]>0,'Invalid extension API string header') end
    local names={}
    for _, class in ipairs(api.classes) do
        assert(type(class)=='table' and type(class.name)=='string' and class.name:match('^[%a_][%w_]*$') and not names[class.name],'Invalid or duplicate extension API class name')
        names[class.name]=true
        assert(class.inherits==nil or (type(class.inherits)=='string' and class.inherits:match('^[%a_][%w_]*$')),'Invalid extension API parent class')
    end
    return output
end

function publish(capture, api, header, generate)
    local directory=path.directory(capture)
    local prepared_header=path.join(directory,path.filename(header))
    assert(prepared_header~=capture and path.absolute(prepared_header)~=path.absolute(header),'SDK staging output must be separate from published files')
    if os.isfile(prepared_header) then os.rm(prepared_header) end
    local oldapi,oldheader=os.isfile(api),os.isfile(header)
    local api_backup,header_backup=path.join(directory,'previous-api.json'),path.join(directory,'previous-sdk.h')
    if oldapi then os.cp(api,api_backup) end
    if oldheader then os.cp(header,header_backup) end
    local ok,failure=utils.trycall(function()
        generate(prepared_header)
        assert(os.isfile(prepared_header) and os.filesize(prepared_header)>0,'SDK generation did not publish a fresh header')
        os.cp(prepared_header,header)
        os.cp(capture,api)
    end)
    if not ok then
        if oldapi then os.cp(api_backup,api) elseif os.isfile(api) then os.rm(api) end
        if oldheader then os.cp(header_backup,header) elseif os.isfile(header) then os.rm(header) end
        raise('%s',failure)
    end
    if os.isfile(api_backup) then os.rm(api_backup) end
    if os.isfile(header_backup) then os.rm(header_backup) end
end
