-- Configure and build actual native xmake targets with reusable configuration caches.
-- Usage: xmake lua misc/scripts/build_egp.lua PLATFORM TARGET JOBS CACHE_DIR "KEY=VALUE ..." [dry-run]
function build_result(root, graph)
    local options = assert(graph.options, "Native graph options missing")
    local program = assert(graph.programs and graph.programs[1], "Native graph program missing")
    local filename = assert(program.filename, "Native graph filename missing")
    assert(filename == path.filename(filename) and filename:startswith("godot."), "Invalid native program filename")
    local mono = options.module_mono_enabled == true
    local mono_library = false
    for _, library in ipairs(graph.libraries or {}) do if library.name == "module_mono" then mono_library = true end end
    assert(mono == mono_library, "Native Mono option/library state mismatch")
    assert((filename:find(".mono", 1, true) ~= nil) == mono, "Native Mono filename/state mismatch")
    return {editor = path.join(root, "bin", filename), platform = options.platform, target = options.target,
        arch = options.arch, precision = options.precision, deprecated = options.deprecated, mono = mono}
end

function main(platform, target, jobs, cache, flags, dryrun, resultpath, invocation_id)
    import('core.base.json')
    import('core.base.bytes')
    local root = path.absolute(path.join(os.scriptdir(), '../..'))
    local policy = import('build.xmake.platforms.init', {rootdir=root})
    local options = {platform=assert(platform), target=target or 'editor'}
    assert(table.contains({'editor','template_debug','template_release'}, options.target), 'Unknown native engine target')
    jobs = tonumber(jobs) or os.default_njob()
    assert(jobs >= 1 and jobs <= 256, 'Jobs must be 1..256')
    local tokens = flags and flags:startswith('[') and json.decode(flags) or os.argv(flags or '')
    for _, token in ipairs(tokens) do
        local key, value = token:match('^([%a_][%w_]*)=(.*)$')
        assert(key and #value > 0, 'Expected KEY=VALUE, received ' .. token)
        options[key] = ({yes='y',no='n',['true']='y',['false']='n'})[value] or value
    end
    assert(options.platform == platform and options.target == target, 'Option platform/target disagrees with launch request')
    options.arch = options.arch or ((platform=='android' or platform=='ios' or platform=='visionos') and 'arm64' or platform=='web' and 'wasm32' or 'x86_64')
    local normalized = policy.normalize(options)
    local keys, ordered = {}, {}
    for key in pairs(options) do table.insert(keys,key) end
    table.sort(keys)
    for _, key in ipairs(keys) do table.insert(ordered, key .. '=' .. tostring(options[key])) end
    local digest = hash.sha256(bytes(table.concat(ordered,'\n')))
    local mode = (policy.enabled(options.dev_build) or target=='template_debug') and 'debug' or 'release'
    local variant = path.join(path.absolute(cache or path.join(root,'.build/xmake-cache')), platform .. '-' .. options.arch .. '-' .. target, mode)
    local configure = {'f','-y','-P',root,'-o',variant,'-p',normalized.plat,'-a',normalized.arch,'--toolchain=' .. normalized.toolchain,'-m',mode,'--godot_platform=' .. platform,'--egp_arch=' .. options.arch,'--egp_target=' .. target}
    table.join2(configure, import('build.xmake.platforms.host', {rootdir=root}).configure_arguments(normalized, options))
    for _, key in ipairs(keys) do
        if key ~= 'platform' and key ~= 'target' and key ~= 'arch' and key ~= 'mingw' then table.insert(configure,'--' .. key .. '=' .. options[key]) end
    end
    if platform == 'android' then
        local ndk = os.getenv('ANDROID_NDK_ROOT') or os.getenv('ANDROID_NDK_HOME') or (os.getenv('ANDROID_HOME') and path.join(os.getenv('ANDROID_HOME'),'ndk/29.0.14206865'))
        if ndk then table.insert(configure,'--ndk=' .. ndk); table.insert(configure,'--ndk_sdkver=24') end
    elseif platform == 'ios' then
        table.insert(configure,'--appledev=' .. (policy.enabled(options.simulator) and 'simulator' or 'iphone'))
        table.insert(configure,'--target_minver=15.0')
    end
    local commands = {configure, {'-P',root,'-b','-j',tostring(jobs),target}}
    if os.getenv('EGP_COMPILE_COMMANDS') == 'true' then table.insert(commands, {'project','-P',root,'-k','compile_commands','.'}) end
    for _, command in ipairs(commands) do
        print('xmake ' .. os.args(command))
        if dryrun ~= 'dry-run' then
            os.mkdir(variant)
            os.vrunv(os.programfile(),command,{curdir=root,envs={XMAKE_CONFIGDIR=path.join(variant,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/engine')}})
        end
    end
    if dryrun ~= 'dry-run' then
        json.savefile(path.join(variant,'invocation.json'),{platform=platform,target=target,options=options,options_sha256=digest,commands=commands})
        if resultpath then
            local graph = json.loadfile(path.join(variant, 'engine-graph.json'))
            local result = build_result(root, graph)
            assert(result.platform == platform and result.target == target, 'Native graph/result request mismatch')
            assert(os.isfile(result.editor), 'Successful native build did not publish its graph-declared program')
            result.invocation_id, result.options_sha256, result.builddir = invocation_id, digest, variant
            json.savefile(path.absolute(resultpath), result)
        end
    end
end
