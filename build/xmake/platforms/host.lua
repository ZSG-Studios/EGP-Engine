function select(options, host, arch, available)
    options=options or {}
    host,arch=host or os.host(),arch or os.arch()
    local enabled=import('init',{rootdir=os.scriptdir()}).enabled
    local native=(host=='windows' and options.platform=='windows') or ((host=='linux' or host=='bsd') and options.platform=='linuxbsd')
    if native and host=='windows' then
        if enabled(options.use_mingw) then return {plat='mingw',arch=arch,toolchain='mingw',clang=enabled(options.use_llvm)} end
        return {plat=host,arch=arch,toolchain=enabled(options.use_llvm) and 'clang-cl' or 'msvc'}
    end
    if native then return {plat=host,arch=arch,toolchain=(enabled(options.use_llvm,host=='linux') or host=='bsd') and 'clang' or 'gcc'} end
    if not available then
        import('lib.detect.find_tool')
        available=function(name) return find_tool(name)~=nil end
    end
    if host=='windows' and available('g++') then return {plat='mingw',arch=arch,toolchain='mingw'} end
    return {plat=host,arch=arch,toolchain=assert(({windows='msvc',linux='clang',macosx='xcode',bsd='clang'})[host],'Unsupported native code-generation host')}
end

-- These standalone generators must not load an unrelated Git/MSYS runtime from PATH.
function configure_runtime(target)
    if target:is_plat('mingw') then target:add('ldflags', {'-static', '-pthread'}, {force=true}) end
end

-- Explicit SDK roots must reach both the engine and isolated native generator projects.
function configure_arguments(normalized, options, sdkroot)
    if normalized.plat ~= 'mingw' then return {} end
    local arguments = normalized.clang and {'--egp_mingw_clang=y'} or {}
    sdkroot = sdkroot or (options and options.mingw) or os.getenv('EGP_MINGW_ROOT')
    if not sdkroot or sdkroot == '' then return arguments end
    sdkroot = path.absolute(sdkroot)
    assert(os.isdir(path.join(sdkroot, 'bin')), 'Configured MinGW SDK has no bin directory: ' .. sdkroot)
    table.insert(arguments, '--mingw=' .. sdkroot)
    return arguments
end
