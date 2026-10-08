set_xmakever('3.1.1')
toolchain('egp_web_argv_probe')
    set_kind('standalone')
    on_check(function () return true end)
    on_load(function (chain)
        import('lib.detect.find_tool')
        local candidates = {}
        if os.host() == 'windows' then
            for _, instance in ipairs(os.dirs('C:/Program Files/Microsoft Visual Studio/*/*')) do
                table.insert(candidates, path.join(instance, 'VC/Tools/Llvm/x64/bin'))
                table.insert(candidates, path.join(instance, 'VC/Tools/Llvm/bin'))
            end
            table.insert(candidates, 'C:/Program Files/LLVM/bin')
        end
        local compiler = assert(find_tool('clang', {paths=candidates}), 'The actual linker-argv fixture requires Clang')
        for _, key in ipairs({'cc', 'ld', 'sh'}) do chain:set('toolset', key, 'clang@' .. compiler.program) end
    end)
toolchain_end()
for _, kind in ipairs({'binary', 'shared'}) do
    target('web_option_pairs_' .. kind)
        set_kind(kind)
        set_toolchains('egp_web_argv_probe')
        on_load(function (target)
            local values = {}
            for index=1,12 do
                table.join2(values, {'--js-library', path.join(os.scriptdir(), "fixture's JS libraries/library " .. index .. '.js')})
            end
            for _, option in ipairs({'--pre-js', '--post-js'}) do
                for index=1,2 do table.join2(values, {option, path.join(os.scriptdir(), "fixture's JS scripts/" .. option .. ' ' .. index .. '.js')}) end
            end
            target:data_set('fixture.flags', values)
            import('build.xmake.linking', {rootdir=path.absolute('../../../..',os.scriptdir())}).flags(target, {LINKFLAGS=values})
        end)
        on_config(function (target)
            import('core.tool.linker')
            local instance = assert(linker.load(kind, {'cc'}, {target=target}))
            local _, argv = instance:linkargv({'probe.o'}, target:targetfile())
            local values = target:data('fixture.flags')
            local checks = 0
            local function check(value, message) assert(value,message); checks=checks+1 end
            local counts = {}
            for index=1,#argv do
                local option = argv[index]
                if option == '--js-library' or option == '--pre-js' or option == '--post-js' then
                    counts[option] = (counts[option] or 0) + 1
                    local found = false
                    for pair=1,#values,2 do if values[pair] == option and values[pair+1] == argv[index+1] then found=true end end
                    check(found, 'Actual linker argv must retain each option with its exact space/apostrophe-containing operand')
                end
            end
            check(counts['--js-library'] == 12 and counts['--pre-js'] == 2 and counts['--post-js'] == 2, 'Every repeated Emscripten option must survive native flag deduplication')
            for index=2,#values,2 do
                local found = 0
                for _, value in ipairs(argv) do if value == values[index] then found=found+1 end end
                check(found == 1, 'No JavaScript library or script may disappear or split at spaces/quotes')
            end
            local old = instance:_preprocess_flags(table.clone(values))
            local old_options = 0
            for _, value in ipairs(old) do if value == '--js-library' then old_options=old_options+1 end end
            check(old_options == 1, 'Old flattened flags must reproduce xmake option deduplication')
            check(#old < #values, 'Negative control must lose the old repeated option/operand structure')
            print('NATIVE_WEB_LINK_FLAG_CHECKS=' .. checks)
        end)
    target_end()
end
