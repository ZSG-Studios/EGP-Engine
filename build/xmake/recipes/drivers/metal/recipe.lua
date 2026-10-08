-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local driver_obj, env, env_metal, env_thirdparty, thirdparty_dir, thirdparty_obj, thirdparty_sources, thirdparty_spirv_headers_dir
    env = graph:use("env")
    env:prepend({["CPPPATH"] = {"#thirdparty/metal-cpp/"}})
    env_metal = env:clone()
    thirdparty_obj = {}
    thirdparty_spirv_headers_dir = "#thirdparty/spirv-headers/"
    thirdparty_dir = "#thirdparty/spirv-cross/"
    thirdparty_sources = {"spirv_cfg.cpp", "spirv_cross.cpp", "spirv_parser.cpp", "spirv_msl.cpp", "spirv_reflect.cpp", "spirv_glsl.cpp", "spirv_cross_parsed_ir.cpp", "spirv_cross_util.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    thirdparty_sources = R.iadd(thirdparty_sources, {"#thirdparty/metal-cpp/metal_cpp.cpp"})
    thirdparty_sources = R.iadd(thirdparty_sources, {"#thirdparty/offset_allocator/offsetAllocator.cpp"})
    env_metal:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "/include")}})
    env_metal:prepend({["CPPPATH"] = {R.add(thirdparty_spirv_headers_dir, "include/spirv/unified1")}})
    if R.truthy((R.contains(R.index(env_metal, "CXXFLAGS"), "-fno-exceptions"))) then
        R.remove(R.index(env_metal, "CXXFLAGS"), "-fno-exceptions")
    end
    env_metal:add({["CXXFLAGS"] = {"-fexceptions"}})
    env_thirdparty = env_metal:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env_metal.drivers_sources = R.iadd(env_metal.drivers_sources, thirdparty_obj)
    if R.truthy((R.contains(R.index(env_metal, "CXXFLAGS"), "-std=gnu++17"))) then
        R.remove(R.index(env_metal, "CXXFLAGS"), "-std=gnu++17")
    end
    env_metal:add({["CXXFLAGS"] = {"-std=gnu++20"}})
    env_metal:add({["CCFLAGS"] = {"-fmodules", "-fcxx-modules"}})
    driver_obj = {}
    env_metal:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
