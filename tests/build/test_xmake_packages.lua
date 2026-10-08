-- Post-link package qualification; uses synthetic binaries and native platform staging.
function main(zipper)
    local package=import('build.xmake.platforms.package',{rootdir=os.curdir()})
    local root=os.curdir()
    local output=path.join(root,'.build/xmake-package-tests')
    os.mkdir(output)
    local android=path.join(output,'android-project')
    local ndk=path.join(output,'android-ndk')
    local stl=path.join(ndk,'toolchains/llvm/prebuilt',os.host()=='windows' and 'windows-x86_64' or os.host()=='macosx' and 'darwin-x86_64' or 'linux-x86_64','sysroot/usr/lib/aarch64-linux-android/libc++_shared.so')
    os.mkdir(path.directory(stl)); io.writefile(stl,'synthetic-stl')
    local library=path.join(output,'android.so'); io.writefile(library,'synthetic-engine')
    package.finish({root=android,bin_dir=output,targetfile=library,options={platform='android',target='editor',arch='arm64',ndk=ndk},zip_executable=zipper})
    local staged=path.join(android,'platform/android/java/lib/libs/tools/debug/arm64-v8a')
    assert(io.readfile(path.join(staged,'libgodot_android.so'))=='synthetic-engine','Android engine JNI staging')
    assert(io.readfile(path.join(staged,'libc++_shared.so'))=='synthetic-stl','Matching shared libc++ staging')
    for _, platform in ipairs({'ios','visionos'}) do
        local archive=path.join(output,platform .. '.a')
        io.writefile(archive,'platform-archive')
        local called=false
        package.finish({root=root,bin_dir=output,targetfile=archive,options={platform=platform},archives={'engine-core.a','engine-scene.a'},run=function(program,args)
            assert(program=='xcrun' and args[1]=='libtool' and args[2]=='-static','Apple static merger must use native libtool')
            assert(args[5]==archive and args[6]=='engine-core.a' and args[7]=='engine-scene.a','Apple merger must include platform and every engine archive')
            io.writefile(args[4],'complete-engine-archive'); called=true
        end})
        assert(called and io.readfile(archive)=='complete-engine-archive','Merged Apple archive must replace platform-only artifact')
    end
    local msvc=path.join(output,'static-engine.lib'); io.writefile(msvc,'platform-object')
    local called=false
    package.finish({root=root,bin_dir=output,targetfile=msvc,options={platform='windows',library_type='static_library'},archives={'engine-core.lib'},archive_tool='lib.exe',run=function(program,args)
        assert(program=='lib.exe' and args[1]=='/NOLOGO' and args[3]==msvc and args[4]=='engine-core.lib','MSVC static export must combine dependency archives')
        io.writefile(args[2]:sub(6),'complete-engine'); called=true
    end})
    assert(called and io.readfile(msvc)=='complete-engine','MSVC static engine must replace the platform-only artifact')
    print('XMAKE_PLATFORM_PACKAGES_PASS 10')
end
