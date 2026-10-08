-- Native common-policy regression checks; no Python build runtime.
function main()
    local common = import('build.xmake.common', {rootdir=os.curdir()})
    local checks = 0
    local function check(value, message) assert(value, message); checks=checks+1 end
    local function configure(overrides)
        local options = {platform='windows',library_type='executable'}
        local explicit = {platform=true, library_type=true}
        for key,value in pairs(overrides or {}) do options[key]=value; explicit[key]=true end
        local env = {graph={root=os.curdir()}}
        function env:add(fields)
            for key,values in pairs(fields) do self[key]=self[key] or {}; for _,value in ipairs(values) do table.insert(self[key],value) end end
        end
        common.configure(env,options,explicit)
        return env,options
    end
    local env,options=configure()
    check(not env.dev_build and options.optimize=='speed_trace','Default editor optimize policy')
    check(not options.debug_symbols,'Nondev symbol default')
    check(table.contains(env.CPPDEFINES,'TOOLS_ENABLED') and table.contains(env.CPPDEFINES,'DEBUG_ENABLED'),'Editor macros')
    check(not table.contains(env.CPPDEFINES,'LIBGODOT_ENABLED'),'Executable default')
    env,options=configure({dev_build=true})
    check(options.debug_symbols and options.optimize=='none','Dev defaults')
    env,options=configure({dev_build=true,debug_symbols=false})
    check(not options.debug_symbols,'Explicit symbol disable')
    env,options=configure({dev_mode=true,werror=false})
    check(options.tests and options.strict_checks and not options.werror,'Dev mode respects explicit override')
    env,options=configure({target='template_release',disable_3d=true})
    check(options.disable_physics_3d and options.disable_xr and table.contains(env.CPPDEFINES,'PHYSICS_3D_DISABLED'),'Disabled dimensions')
    check(not utils.trycall(function() configure({rendering_device=false}) end),'Forward+ requires RenderingDevice')
    check(not utils.trycall(function() configure({platform="web"}) end),'WebGL exports must be rejected')
    env,options=configure({platform='linuxbsd',arch='x86'})
    check(options.arch=='x86_32' and table.contains(env.CCFLAGS,'-mfpmath=sse'),'x86 math determinism')
    env,options=configure({target='template_debug',dev_build=true,precision='double',threads=false,use_llvm=true,use_asan=true,extra_suffix='custom'})
    env.module_version_string='.mono'; common.suffix(env,options)
    check(env.PROGSUFFIX=='.windows.template_debug.dev.double.x86_64.nothreads.llvm.custom.san.mono.exe','Full output suffix parity')
    env,options=configure({platform='macos',use_llvm=true,extra_suffix='custom',use_asan=true})
    check(env.extra_suffix=='.custom.san','Apple naming excludes LLVM suffix')
    env,options=configure({production=true,dev_build=true})
    check(not options.debug_symbols and options.lto=='auto','Production symbol defaults')
    print('XMAKE_COMMON_POLICY_PASS ' .. checks)
end
