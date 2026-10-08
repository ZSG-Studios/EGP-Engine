-- Generated headers included from source headers need their original include directory.
function configure(target, options, generated)
    local enabled=import('platforms.init',{rootdir=os.scriptdir()}).enabled
    if options.platform=='linuxbsd' and enabled(options.wayland,true) then
        target:add('includedirs',path.join(generated,'platform/linuxbsd/wayland'))
    end
end
