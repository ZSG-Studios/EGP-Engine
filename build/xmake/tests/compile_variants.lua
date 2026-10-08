-- Actual compiler/linker coverage of two policies applied to one C pathname.
function main()
    local root=os.projectdir()
    local project=path.join(root,'build/xmake/tests/compile_variants')
    local directory=path.join(root,'.build/xmake-compile-variants')
    local envs={XMAKE_CONFIGDIR=path.join(directory,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-compile-variants')}
    local compiler=assert(({windows='msvc',linux='gcc',macosx='xcode',bsd='clang'})[os.host()])
    local function invoke(name,args)
        local stdout,stderr=os.iorunv(os.programfile(),args,{curdir=project,envs=envs,timeout=120000})
        os.mkdir(directory); io.writefile(path.join(directory,name .. '.log'),stdout .. (stderr or ''))
        return stdout .. (stderr or '')
    end
    invoke('configure',{'f','-y','--toolchain=' .. compiler,'-P',project,'-o',directory})
    local build=invoke('build',{'-P',project,'-b','-j','1'})
    assert(build:find('NATIVE_COMPILE_VARIANT_GRAPH_CHECKS=6',1,true))
    local snapshots={}
    local objects=import('core.base.json').loadfile(path.join(directory,'linked-objects.json'))
    assert(#objects==5)
    for _, object in ipairs(objects) do
        object=path.absolute(object,project)
        snapshots[object]={mtime=os.mtime(object),hash=hash.sha256(object)}
    end
    invoke('warm',{'-P',project,'-b','-j','1'})
    for object,state in pairs(snapshots) do assert(os.mtime(object)==state.mtime and hash.sha256(object)==state.hash,'Warm build must preserve ordinary and variant object caches') end
    local run=invoke('run',{'run','-P',project,'egp_compile_variants_probe'})
    assert(run:find('NATIVE_COMPILE_VARIANT_RUNTIME_PASS 8',1,true),'Both synthetic and actual JPEG8/12 symbols must link and behave correctly')
    import('core.base.json').savefile(path.join(directory,'receipt.json'),{passed=true,checks=16,host=os.host(),compiler=compiler,objects=snapshots,jpeg_bits={8,12},warm_objects_unchanged=true})
    print('NATIVE_COMPILE_VARIANT_CHECKS=16')
end
