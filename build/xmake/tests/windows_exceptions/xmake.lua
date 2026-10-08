set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags",false)

for _,compiler_name in ipairs({"msvc","clang-cl"}) do
    for _,profile in ipairs({{name="default"},{name="disabled",disabled=true},{name="enabled",disabled=false}}) do
        local name="exception_" .. compiler_name:gsub("%-","_") .. "_" .. profile.name
        target(name)
            set_kind("object")
            add_files("probe.cpp")
            on_load(function(target)
                import("build.xmake.platforms.init",{rootdir=path.absolute("../../../..",os.scriptdir())}).configure(target,{
                    platform="windows",arch="x86_64",use_llvm=compiler_name=="clang-cl",disable_exceptions=profile.disabled,
                    debug_symbols=false,werror=true,accesskit=false,angle=false,d3d12=false})
                target:add("defines","EGP_EXPECT_EXCEPTIONS=" .. (profile.disabled==false and "1" or "0"))
            end)
            on_config(function(target)
                local enabled=profile.disabled==false
                assert(target:get("exceptions")== (enabled and "cxx" or "no-cxx"),"The engine exception option must override xmake's Windows default")
                local compiler=assert(import("core.tool.compiler").load("cxx",{target=target}))
                local source=path.join(os.scriptdir(),"probe.cpp")
                local object=path.join(path.absolute(import("core.project.config").builddir(),os.projectdir()),name .. ".obj")
                os.mkdir(path.directory(object))
                local program,arguments=compiler:compargv(source,object,{target=target})
                assert(table.contains(arguments,enabled and "/EHsc" or "/EHs-c-"),"The installed compiler driver must receive the requested exception policy")
                assert(not table.contains(arguments,enabled and "/EHs-c-" or "/EHsc"),"The compiler must not retain xmake's conflicting implicit exception policy")
                os.vrunv(program,arguments,{envs=compiler:runenvs(),timeout=60000})
                assert(os.isfile(object),"The actual compiler must agree with the option through its _CPPUNWIND macro")
                print("NATIVE_WINDOWS_EXCEPTION_CASE_PASS=" .. name)
            end)
        target_end()
    end
end
