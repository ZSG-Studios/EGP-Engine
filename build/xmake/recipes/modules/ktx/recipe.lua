-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_ktx, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_ktx = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/libktx/"
    thirdparty_sources = {"lib/basis_transcode.cpp", "lib/checkheader.c", "lib/filestream.c", "lib/hashlist.c", "lib/memstream.c", "lib/miniz_wrapper.cpp", "lib/swap.c", "lib/texture.c", "lib/texture1.c", "lib/texture2.c", "lib/vkformat_check.c", "lib/vkformat_check_variant.c", "lib/vkformat_typesize.c", "external/dfdutils/createdfd.c", "external/dfdutils/colourspaces.c", "external/dfdutils/interpretdfd.c", "external/dfdutils/printdfd.c", "external/dfdutils/queries.c", "external/dfdutils/vk2dfd.c"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_ktx:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "include")}})
    env_ktx:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "utils")}})
    env_ktx:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "lib")}})
    env_ktx:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "other_include")}})
    env_ktx:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "external")}})
    env_ktx:prepend({["CPPPATH"] = {"#thirdparty/basis_universal"}})
    if R.truthy(env.editor_build) then
        env_ktx:add({["CPPDEFINES"] = {"MINIZ_HEADER_FILE_ONLY"}})
    end
    if R.truthy(R.index(env, "vulkan")) then
        env_ktx:prepend({["CPPPATH"] = {"#thirdparty/vulkan/include"}})
    else
        env_ktx:add({["CPPDEFINES"] = {"LIBKTX"}})
    end
    env_ktx:add({["CPPDEFINES"] = {{"KHRONOS_STATIC", 1}}})
    env_thirdparty = env_ktx:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_ktx:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
