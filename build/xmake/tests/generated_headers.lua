-- Compile an editor-side consumer of a source header with a generated protocol include.
function main()
    local root=os.curdir()
    local project=path.join(root,'build/xmake/tests/generated_headers')
    local directory=path.join(root,'.build/xmake-generated-headers')
    local compiler=assert(({windows='msvc',linux='clang',macosx='xcode',bsd='clang'})[os.host()])
    local envs={XMAKE_CONFIGDIR=path.join(directory,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-generated-headers')}
    local stdout=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. compiler,'-P',project,'-o',directory},{curdir=project,envs=envs})
    assert(stdout:find('NATIVE_GENERATED_HEADER_POLICY_CHECKS=2',1,true))
    os.iorunv(os.programfile(),{'-P',project,'-b','-j','1'},{curdir=project,envs=envs,timeout=120000})
    os.iorunv(os.programfile(),{'run','-P',project,'generated_header_probe'},{curdir=project,envs=envs,timeout=10000})
    print('NATIVE_GENERATED_HEADER_CHECKS=4')
end
