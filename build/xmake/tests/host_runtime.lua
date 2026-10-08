-- Execute the real helper binaries against independent binary/zlib/ZIP fixtures.
function main(compressor, zipper)
    local root=os.curdir()
    local hosts=import('build.xmake.platforms.host',{rootdir=root})
    local checks=0
    local function check(value,message) assert(value,message); checks=checks+1 end
    for _,platform in ipairs({'mingw','windows','linux','macosx'}) do
        local calls={}
        hosts.configure_runtime({is_plat=function(_,name) return name==platform end,
            add=function(_,name,value,options) table.insert(calls,{name,value,options}) end})
        check(#calls==(platform=='mingw' and 1 or 0),'Only standalone MinGW helpers may receive static GNU runtimes')
        if platform=='mingw' then
            check(calls[1][1]=='ldflags' and table.concat(calls[1][2],',')=='-static,-pthread' and calls[1][3].force,'GNU runtime isolation must reach GCC and Clang linkers with static thread support')
        end
    end
    if os.host()=='windows' then
        for _,program in ipairs({compressor,zipper}) do
            local image=assert(io.readfile(program,{encoding='binary'}))
            local function number(offset,size)
                local value=0
                for index=0,size-1 do value=value+assert(image:byte(offset+index))*256^index end
                return value
            end
            check(image:sub(1,2)=='MZ','Windows helpers must be real PE binaries')
            local pe=number(61,4)+1
            check(image:sub(pe,pe+3)=='PE\0\0','PE header must be complete')
            local sections,optional=number(pe+6,2),pe+24
            local imports=optional+(number(optional,2)==0x20b and 120 or 104)
            local table_rva=number(imports,4)
            local section=optional+number(pe+20,2)
            local function physical(rva)
                for index=0,sections-1 do
                    local entry=section+index*40
                    local start,size=number(entry+12,4),math.max(number(entry+8,4),number(entry+16,4))
                    if rva>=start and rva<start+size then return number(entry+20,4)+rva-start+1 end
                end
                error('PE import RVA must resolve to a section')
            end
            local cursor=physical(table_rva)
            local names={}
            while number(cursor+12,4)~=0 do
                local name=physical(number(cursor+12,4))
                table.insert(names,assert(image:match('([^%z]+)',name)):lower())
                cursor=cursor+20
            end
            check(#names>0,'Actual PE import descriptors must be inspected')
            for _,name in ipairs({'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll'}) do
                check(not table.contains(names,name),'Standalone helpers must not resolve ambiguous GNU runtime DLLs from PATH: ' .. name)
            end
        end
    end
    local directory=path.join(root,'.build/host-runtime-fixture')
    local inputs=path.join(directory,'inputs')
    os.mkdir(inputs)
    local function unhex(value) return (value:gsub('%x%x',function(pair) return string.char(tonumber(pair,16)) end)) end
    local payload=unhex('6e61746976652068656c70657200555446383a20636166c3a90a')
    local cases={
        {name='empty.bin',bytes='',zlib='78da030000000001',raw='0300',crc='00000000'},
        {name='binary payload.bin',bytes=payload,zlib='78dacb4b2cc92c4b55c848cd29482d62080d71b3b052484e4c3bbc920b007f900949',
            raw='cb4b2cc92c4b55c848cd29482d62080d71b3b052484e4c3bbc920b00',crc='b4c418b4'}
    }
    local envs
    if os.host()=='windows' then
        local git=path.join(os.getenv('ProgramFiles') or 'C:/Program Files','Git/mingw64/bin')
        if os.isdir(git) then envs={PATH=git .. ';' .. os.getenv('PATH')} end
    end
    for _,case in ipairs(cases) do
        local input=path.join(inputs,case.name)
        io.writefile(input,case.bytes,{encoding='binary'})
        for _,format in ipairs({'zlib','raw'}) do
            local output=path.join(directory,case.name .. '.' .. format)
            os.vrunv(assert(compressor),{'--input',input,'--output',output,'--format',format},{envs=envs})
            check(io.readfile(output,{encoding='binary'})==unhex(case[format]),'Actual compression must match independently decoded binary fixtures under ambient DLL PATH')
        end
        local crc=os.iorunv(compressor,{'--input',input,'--crc32'},{envs=envs})
        check(crc:trim()==case.crc,'Actual CRC must preserve empty, NUL and UTF-8 input bytes')
    end
    local output=path.join(directory,'payload.zip')
    os.vrunv(assert(zipper),{'--root',inputs,'--output',output},{envs=envs})
    local archive=assert(io.readfile(output,{encoding='binary'}))
    local function word(offset,size)
        local value=0
        for index=0,size-1 do value=value+archive:byte(offset+index)*256^index end
        return value
    end
    local ordered={cases[2],cases[1]}
    local offset=1
    for _,case in ipairs(ordered) do
        check(archive:sub(offset,offset+3)=='PK\3\4','ZIP must contain each actual local file record')
        check(word(offset+8,2)==8,'ZIP payload must use raw DEFLATE')
        check(word(offset+14,4)==tonumber(case.crc,16),'ZIP CRC must equal the independent payload checksum')
        check(word(offset+22,4)==#case.bytes,'ZIP uncompressed size must retain every byte')
        local size,namesize,extra=word(offset+18,4),word(offset+26,2),word(offset+28,2)
        check(archive:sub(offset+30,offset+29+namesize)==case.name,'ZIP entries must retain sorted space-containing file names')
        local start=offset+30+namesize+extra
        check(archive:sub(start,start+size-1)==unhex(case.raw),'ZIP raw payload must match the independent DEFLATE fixture')
        offset=start+size
    end
    check(archive:sub(offset,offset+3)=='PK\1\2','Both file records must be followed by the central directory')
    check(archive:sub(-22,-19)=='PK\5\6','ZIP must contain a complete end-of-central-directory record')
    local second=path.join(directory,'payload-again.zip')
    os.vrunv(zipper,{'--root',inputs,'--output',second},{envs=envs})
    check(io.readfile(second,{encoding='binary'})==archive,'Repeated native ZIP builds must preserve identical bytes')
    print('NATIVE_HOST_RUNTIME_CHECKS=' .. checks)
end
