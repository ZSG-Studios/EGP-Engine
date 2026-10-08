-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local driver_obj, env, env_thirdparty_respirv, env_thirdparty_vma, env_thirdparty_volk, thirdparty_dir, thirdparty_obj, thirdparty_respirv_dir, thirdparty_sources_respirv, thirdparty_sources_vma, thirdparty_sources_volk, thirdparty_spirv_headers_dir, thirdparty_volk_dir
    env = graph:use("env")
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/vulkan"
    thirdparty_volk_dir = "#thirdparty/volk"
    thirdparty_spirv_headers_dir = "#thirdparty/spirv-headers"
    thirdparty_respirv_dir = "#thirdparty/re-spirv"
    env:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "/include")}})
    if R.truthy(R.index(env, "use_volk")) then
        env:add_unique({["CPPDEFINES"] = {"USE_VOLK"}})
        env:prepend({["CPPPATH"] = {thirdparty_volk_dir}})
    end
    if R.truthy(((R.index(env, "platform") == "android"))) then
        env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_ANDROID_KHR"}})
    else
        if R.truthy(((R.index(env, "platform") == "ios"))) then
            env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_IOS_MVK", "VK_USE_PLATFORM_METAL_EXT"}})
        else
            if R.truthy(((R.index(env, "platform") == "linuxbsd"))) then
                if R.truthy(R.index(env, "x11")) then
                    env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_XLIB_KHR"}})
                end
                if R.truthy(R.index(env, "wayland")) then
                    env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_WAYLAND_KHR"}})
                end
            else
                if R.truthy(((R.index(env, "platform") == "macos"))) then
                    env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_MACOS_MVK", "VK_USE_PLATFORM_METAL_EXT"}})
                else
                    if R.truthy(((R.index(env, "platform") == "windows"))) then
                        env:add_unique({["CPPDEFINES"] = {"VK_USE_PLATFORM_WIN32_KHR"}})
                    end
                end
            end
        end
    end
    env:add_unique({["CPPDEFINES"] = {"VMA_EXTERNAL_MEMORY_WIN32=0"}})
    env_thirdparty_vma = env:clone()
    env_thirdparty_vma:disable_warnings()
    thirdparty_sources_vma = {R.add(thirdparty_dir, "/vk_mem_alloc.cpp")}
    if R.truthy(R.index(env, "use_volk")) then
        env_thirdparty_vma:add_unique({["CPPDEFINES"] = {{"VMA_STATIC_VULKAN_FUNCTIONS", 1}}})
        env_thirdparty_volk = env:clone()
        env_thirdparty_volk:disable_warnings()
        thirdparty_sources_volk = {R.add(thirdparty_volk_dir, "/volk.c")}
        env_thirdparty_volk:sources(thirdparty_obj, thirdparty_sources_volk)
    else
        if R.truthy(((R.index(env, "platform") == "android"))) then
            env_thirdparty_vma:add_unique({["CPPDEFINES"] = {{"VMA_VULKAN_VERSION", 1000000}}})
        else
            if R.truthy((function() local v = ((R.index(env, "platform") == "macos")); if R.truthy(v) then return v end; return ((R.index(env, "platform") == "ios")) end)()) then
                env_thirdparty_vma:add_unique({["CPPDEFINES"] = {{"VMA_VULKAN_VERSION", 1001000}}})
            end
        end
    end
    env_thirdparty_vma:sources(thirdparty_obj, thirdparty_sources_vma)
    env_thirdparty_respirv = env:clone()
    env_thirdparty_respirv:prepend({["CPPPATH"] = {R.add(thirdparty_spirv_headers_dir, "/include")}})
    env_thirdparty_respirv:disable_warnings()
    thirdparty_sources_respirv = {R.add(thirdparty_respirv_dir, "/re-spirv.cpp")}
    env_thirdparty_respirv:sources(thirdparty_obj, thirdparty_sources_respirv)
    env.drivers_sources = R.iadd(env.drivers_sources, thirdparty_obj)
    driver_obj = {}
    env:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
