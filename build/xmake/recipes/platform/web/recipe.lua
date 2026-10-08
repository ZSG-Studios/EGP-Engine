-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local json = import("core.base.json")
    local build, build_targets, env, ext, js, lib, sys, sys_env, wasm, web_files
    env = graph:use("env")
    web_files = {"audio_driver_web.cpp", "webmidi_driver.cpp", "display_server_web.cpp", "http_client_web.cpp", "javascript_bridge_singleton.cpp", "remote_debugger_peer_messageport.cpp", "web_main.cpp", "os_web.cpp"}
    if R.truthy(((R.index(env, "target") == "editor"))) then
        env:sources(web_files, "editor/*.cpp")
    end
    sys_env = env:clone()
    sys_env:AddJSLibraries({"js/libs/library_godot_audio.js", "js/libs/library_godot_display.js", "js/libs/library_godot_emscripten.js", "js/libs/library_godot_fetch.js", "js/libs/library_godot_webmidi.js", "js/libs/library_godot_os.js", "js/libs/library_godot_runtime.js", "js/libs/library_godot_input.js", "js/libs/library_godot_webgl2.js", "js/libs/library_godot_debugger.js"})
    sys_env:AddJSExterns({"js/libs/library_godot_webgl2.externs.js"})
    sys_env:AddJSPost({"js/patches/patch_em_gl.js"})
    if R.truthy(R.index(env, "javascript_eval")) then
        sys_env:AddJSLibraries({"js/libs/library_godot_javascript_singleton.js"})
    end
    if R.truthy(((R.index(env, "target") == "editor"))) then
        sys_env:AddJSLibraries({"js/libs/library_godot_editor_debugger.js"})
    end
    for _, __item2 in ipairs(R.iter(R.index(sys_env, "JS_LIBS"))) do
        lib = __item2
        sys_env:add({["LINKFLAGS"] = {"--js-library", lib.abspath}})
    end
    for _, __item3 in ipairs(R.iter(R.index(sys_env, "JS_PRE"))) do
        js = __item3
        sys_env:add({["LINKFLAGS"] = {"--pre-js", js.abspath}})
    end
    for _, __item4 in ipairs(R.iter(R.index(sys_env, "JS_POST"))) do
        js = __item4
        sys_env:add({["LINKFLAGS"] = {"--post-js", js.abspath}})
    end
    R.setindex(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS", R.get(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS", ""))
    for _, __item5 in ipairs(R.iter(R.index(sys_env, "JS_EXTERNS"))) do
        ext = __item5
        R.setindex(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS", R.iadd(R.index(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS"), R.add(" --externs ", ext.abspath)))
    end
    R.setindex(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS", R.strip(R.index(R.index(sys_env, "ENV"), "EMCC_CLOSURE_ARGS")))
    if R.truthy(R.len(R.index(env, "EXPORTED_FUNCTIONS"))) then
        sys_env:add({["LINKFLAGS"] = {R.add("-sEXPORTED_FUNCTIONS=", json.encode(R.sorted(R.list(R.set(R.index(env, "EXPORTED_FUNCTIONS"))))))}})
    end
    if R.truthy(R.len(R.index(env, "EXPORTED_RUNTIME_METHODS"))) then
        sys_env:add({["LINKFLAGS"] = {R.add("-sEXPORTED_RUNTIME_METHODS=", json.encode(R.sorted(R.list(R.set(R.index(env, "EXPORTED_RUNTIME_METHODS"))))))}})
    end
    sys_env:add({["LINKFLAGS"] = {"-sMODULARIZE=1", "-sEXPORT_NAME='Godot'"}})
    build = {}
    build_targets = {"#bin/godot${PROGSUFFIX}.js", "#bin/godot${PROGSUFFIX}.wasm"}
    if R.truthy(R.index(env, "dlink_enabled")) then
        R.setindex(sys_env, "LIBS", {})
        sys_env:add({["LIBS"] = {"idbfs.js"}})
        if R.contains(sys_env.CCFLAGS, "-sSIDE_MODULE=2") then R.remove(sys_env.CCFLAGS, "-sSIDE_MODULE=2") end
        if R.contains(sys_env.LINKFLAGS, "-sSIDE_MODULE=2") then R.remove(sys_env.LINKFLAGS, "-sSIDE_MODULE=2") end
        sys_env:add({["CCFLAGS"] = {"-sMAIN_MODULE=1"}})
        sys_env:add({["LINKFLAGS"] = {"-sMAIN_MODULE=1"}})
        sys_env:add({["LINKFLAGS"] = {"-sEXPORT_ALL=1"}})
        sys_env:add({["LINKFLAGS"] = {"-sWARN_ON_UNDEFINED_SYMBOLS=0"}})
        if R.contains(sys_env.CCFLAGS, "-fvisibility=hidden") then R.remove(sys_env.CCFLAGS, "-fvisibility=hidden") end
        if R.contains(sys_env.LINKFLAGS, "-fvisibility=hidden") then R.remove(sys_env.LINKFLAGS, "-fvisibility=hidden") end
        sys_env:add({["CCFLAGS"] = {"-fwasm-exceptions"}})
        sys_env:add({["LINKFLAGS"] = {"-fwasm-exceptions"}})
        sys = sys_env:program(build_targets, {"web_runtime.cpp"})
        wasm = env:program("#bin/godot.side${PROGSUFFIX}.wasm", web_files)
        build = R.add(sys, {R.index(wasm, 0)})
    else
        sys_env:add({["LIBS"] = {"idbfs.js"}})
        build = sys_env:program(build_targets, R.add(web_files, {"web_runtime.cpp"}))
    end
    sys_env:depends(R.index(build, 0), R.index(sys_env, "JS_LIBS"))
    sys_env:depends(R.index(build, 0), R.index(sys_env, "JS_PRE"))
    sys_env:depends(R.index(build, 0), R.index(sys_env, "JS_POST"))
    sys_env:depends(R.index(build, 0), R.index(sys_env, "JS_EXTERNS"))
    -- Engine wrappers, Closure compilation and template archives run after linking.
end
