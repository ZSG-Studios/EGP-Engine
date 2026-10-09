-- Called only by EGP's module_superpos library branch after captured sources.
-- Source ownership comes from the one versioned core feature, not a second list.
local function cxx23_flags(flags)
    local result={}
    for _,flag in ipairs(flags or {}) do
        if flag=="/d2archSSE42" then
            table.insert(result,"/clang:-msse4.2") -- preserve the captured engine CPU minimum
        elseif not flag:startswith("/std:c++") and not flag:startswith("-std:c++")
           and not flag:startswith("-std=c++") and not flag:startswith("/clang:-std=") then
            table.insert(result,flag)
        end
    end
    table.insert(result,"/clang:-std=c++23")
    return result
end

local function selection(library, options, engine_root)
    local config=import("core.project.config")
    local json=import("core.base.json")
    assert(library.name=="module_superpos","Embedded RTC hook must remain scoped to Superpos")
    assert(options.platform=="windows" and options.arch=="x86_64" and options.threads
           and options.module_mbedtls_enabled and options.builtin_mbedtls,
           "Unsupported captured engine RTC policy")
    local root=path.join(engine_root,"modules/superpos/core")
    local manifest=json.loadfile(path.join(root,"source_manifest.json"))
    local feature=assert(manifest.features.rtc_embedded,"Missing embedded RTC feature")
    assert(feature.version==1 and config.get("superpos_rtc_runtime")==feature.crt,"Embedded RTC policy/CRT mismatch")
    local base,allocator,seen=nil,0,{}
    for _,source in ipairs(library.sources) do
        assert(not seen[source.path],"Duplicate captured module source");seen[source.path]=true
        if source.path=="modules/superpos/core/src/allocator.cpp" then allocator=allocator+1 end
        if source.path=="modules/superpos/core/src/session.cpp" then base=source.policy end
    end
    assert(allocator==1 and base,"Captured engine must own one allocator and core Session policy")
    local extra={}
    local body_policy=table.clone(base)
    body_policy.CXXFLAGS=cxx23_flags(base.CXXFLAGS)

    for _,name in ipairs(feature.module_sources) do
        assert(name:startswith("services/src/") and name:endswith(".cpp") and not name:find("..",1,true),"Invalid embedded body source")
        local filename="modules/superpos/core/" .. name
        assert(not seen[filename],"Duplicate captured/optional source ownership");seen[filename]=true
        table.insert(extra,{path=filename,policy=body_policy}) -- exact captured core compile policy
    end
    local module_manifest=json.loadfile(path.join(engine_root,"modules/superpos/source_manifest.json"))
    local native_feature=assert(module_manifest.features and module_manifest.features.rtc_embedded,"Missing module-owned embedded RTC sources")
    assert(native_feature.core_feature=="rtc_embedded@1","Module/core embedded RTC version mismatch")
    for _,name in ipairs(native_feature.sources) do
        assert(name:startswith("rtc_private/") and name:endswith(".cpp") and not name:find("..",1,true),"Invalid native RTC body")
        local filename="modules/superpos/" .. name
        assert(not seen[filename],"Duplicate native RTC source");seen[filename]=true
        table.insert(extra,{path=filename,policy=body_policy})
    end
    if config.get("superpos_lifecycle_fixture") then
        local lifecycle=assert(module_manifest.features.native_lifecycle,"Missing native lifecycle feature")
        local name=assert(lifecycle.fixture_source,"Missing lifecycle fixture source")
        assert(name=="private/lifecycle_engine/staged/engine_fixture.cpp","Unsupported lifecycle fixture source")
        local filename="modules/superpos/"..name
        assert(not seen[filename],"Duplicate native lifecycle fixture source")
        table.insert(extra,{path=filename,policy=body_policy})
    end
    return extra,feature,root
end

-- Do not alter the captured descriptor or default-off policies. Opt-in module
-- wrappers also include C++23 core headers; pin the revision for all its bodies.
function policies(list)
    local result={}
    for _,source in ipairs(list) do
        local row=table.clone(source);row.policy=table.clone(source.policy)
        row.policy.CXXFLAGS=cxx23_flags(source.policy.CXXFLAGS)
        table.insert(result,row)
    end
    return result
end

-- Project sources before engine-graph.json is saved, so capture and build agree.
function capture(graph, engine_root)
    local config=import("core.project.config")
    local json=import("core.base.json")
    local memory=json.loadfile(path.join(engine_root,"modules/superpos/source_manifest.json")).memory_service
    assert(memory and memory.version==1 and memory.always_selected,"Missing common module memory feature")
    for _,library in ipairs(graph.libraries) do
        if library.name=="module_superpos" then
            local base,seen=nil,{}
            for _,source in ipairs(library.sources) do
                assert(not seen[source.path], "Duplicate Superpos source in captured graph")
                seen[source.path]=true
                if source.path=="modules/superpos/core/src/session.cpp" then base=source.policy end
            end
            assert(base,"Common memory source requires captured core Session policy")
            for _,name in ipairs(memory.sources) do
                assert(name:startswith("private/") and name:endswith(".cpp") and not name:find("..",1,true),"Invalid common memory source")
                local filename="modules/superpos/"..name
                -- The native recipe now selects the complete adapter manifest.
                -- Retain support for older captured recipes without selecting twice.
                if not seen[filename] then
                    local policy=table.clone(base);policy.CXXFLAGS=table.copy(base.CXXFLAGS)
                    if graph.options.platform=="windows" and graph.options.use_llvm then
                        table.insert(policy.CXXFLAGS,"/clang:-std=c++23")
                    end
                    table.insert(library.sources,{path=filename,policy=policy});seen[filename]=true
                end
            end
            library.module_memory={version=1,sources=memory.sources,manifest_sha256=hash.sha256(path.join(engine_root,"modules/superpos/source_manifest.json"))}
        end
    end
    assert(not config.get("superpos_rtc_fixture") or config.get("superpos_rtc_embedded"), "RTC fixture requires the embedded RTC feature")
    if config.get("superpos_rtc_fixture") then
        assert(graph.options.target=="editor" and graph.options.dev_build==true and config.get("mode")=="debug",
               "RTC fixture requires the debug development editor")
    end
    if config.get("superpos_lifecycle_fixture") then
        assert(graph.options.target=="editor" and graph.options.dev_build==true and config.get("mode")=="debug",
               "Native lifecycle fixture requires the debug development editor")
        -- The fixture bodies are portable. Without embedded RTC, select the
        -- fixture source and its define in the captured module sources here;
        -- the RTC path adds both through its own selection and configure.
        if not config.get("superpos_rtc_embedded") then
            local module_manifest=json.loadfile(path.join(engine_root,"modules/superpos/source_manifest.json"))
            local lifecycle=assert(module_manifest.features and module_manifest.features.native_lifecycle,"Missing native lifecycle feature")
            local name=assert(lifecycle.fixture_source,"Missing lifecycle fixture source")
            assert(name=="private/lifecycle_engine/staged/engine_fixture.cpp","Unsupported lifecycle fixture source")
            local filename="modules/superpos/"..name
            local found=false
            for _,library in ipairs(graph.libraries) do
                if library.name=="module_superpos" then
                    local base,present=nil,false
                    for _,source in ipairs(library.sources) do
                        -- Clone before adding the define: policies may be shared tables.
                        source.policy=table.clone(source.policy);source.policy.CPPDEFINES=table.copy(source.policy.CPPDEFINES or {})
                        table.insert(source.policy.CPPDEFINES,{"SUPERPOS_LIFECYCLE_FIXTURE",1})
                        if source.path=="modules/superpos/superpos_session.cpp" then base=source.policy end
                        if source.path==filename then present=true end
                    end
                    assert(base,"Lifecycle fixture requires the captured Session source policy")
                    if not present then
                        local policy=table.clone(base);policy.CPPDEFINES=table.copy(base.CPPDEFINES)
                        table.insert(library.sources,{path=filename,policy=policy})
                    end
                    found=true
                end
            end
            assert(found,"Captured graph lacks Superpos module")
        end
    end
    if not config.get("superpos_rtc_embedded") then return end
    local found=false
    for _,library in ipairs(graph.libraries) do
        if library.name=="module_superpos" then
            assert(not library.rtc_embedded,"RTC feature captured twice")
            local extra,feature,root=selection(library,graph.options,engine_root)
            local count=#library.sources
            library.sources=policies(library.sources)
            for _,source in ipairs(extra) do table.insert(library.sources,source) end
            library.rtc_embedded={version=1,base_source_count=count,extra_sources=extra,
                manifest_sha256=hash.sha256(path.join(root,"source_manifest.json")),
                backend_target="superpos_egp_rtc_backend",provider_targets=feature.providers.products}
            found=true
        end
    end
    assert(found,"Captured graph lacks Superpos module")
end

function configure(target, library, options, engine_root)
    local config=import("core.project.config")
    local json=import("core.base.json")
    local root=path.join(engine_root,"modules/superpos/core")
    local feature=assert(json.loadfile(path.join(root,"source_manifest.json")).features.rtc_embedded)
    local captured=assert(library.rtc_embedded,"RTC source feature must be selected before graph capture")
    assert(captured.version==1 and captured.manifest_sha256==hash.sha256(path.join(root,"source_manifest.json")),"Captured RTC feature changed")
    import("modules.superpos.rtc_toolchain",{rootdir=engine_root}).configure(target,options)
    target:add("deps",captured.backend_target,{inherit=false})
    target:add("defines","SUPERPOS_HAS_RTC=1")
    if config.get("superpos_rtc_fixture") then target:add("defines","SUPERPOS_RTC_EMBEDDED_FIXTURE=1") end
    if config.get("superpos_lifecycle_fixture") then target:add("defines","SUPERPOS_LIFECYCLE_FIXTURE=1") end
    target:set("runtimes",feature.crt)
    target:add("cxxflags","/GR-",{force=true})
    target:add("includedirs",path.join(engine_root,"modules/superpos"))
    for _,directory in ipairs(feature.include_dirs) do target:add("includedirs",path.join(root,directory)) end
end

-- Keep provider usage private to backend while linking its exact captured products.
function link(target, graph, program)
    local selected=false
    for _,reference in ipairs(program.policy.LIBS or {}) do
        if reference.kind=="target" and reference.name=="module_superpos" then selected=true end
    end
    if not selected then return end
    local config=import("core.project.config")
    local module
    for _,library in ipairs(graph.libraries) do if library.name=="module_superpos" then module=library end end
    local feature=assert(module and module.rtc_embedded,"Missing captured embedded RTC link closure")
    local names={feature.backend_target}
    for _,name in ipairs(feature.provider_targets) do table.insert(names,name) end
    for _,name in ipairs(names) do
        target:add("deps",name,{inherit=false})
        target:add("links",name)
    end
    -- These embedded static targets retain xmake's standard targetdir; guard actual
    -- archive outputs and explicit link references in the normal graph dry-run.
    target:add("linkdirs",path.join(path.absolute(config.builddir()),target:plat(),target:arch(),config.get("mode")))
end
