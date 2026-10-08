-- Target toolchain configs include the arguments in their cache identity.
-- Never mutate the process environment or serialize unrelated host variables.
function configure(target, toolchain_name, closure_args)
    assert(type(closure_args)=="string", "Closure compiler arguments must be a string")
    local toolchain=assert(target:toolchain(toolchain_name), "Requested Emscripten toolchain missing")
    local current=toolchain:get("runenvs",{load=false}) or {}
    local existing=current.EMCC_CLOSURE_ARGS
    if type(existing)=="table" then assert(#existing==1,"Closure arguments must be a single environment value"); existing=existing[1] end
    if existing ~= closure_args then
        assert(existing==nil, "Closure arguments must be configured before the toolchain is shared")
        toolchain:add("runenvs", "EMCC_CLOSURE_ARGS", closure_args)
    end
end
