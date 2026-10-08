-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_svg, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_svg = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/thorvg/"
    thirdparty_sources = {"src/common/tvgColor.cpp", "src/common/tvgCompressor.cpp", "src/common/tvgMath.cpp", "src/common/tvgStr.cpp", "src/loaders/svg/tvgSvgCssStyle.cpp", "src/loaders/svg/tvgSvgLoader.cpp", "src/loaders/svg/tvgSvgPath.cpp", "src/loaders/svg/tvgSvgSceneBuilder.cpp", "src/loaders/svg/tvgSvgUtil.cpp", "src/loaders/svg/tvgXmlParser.cpp", "src/loaders/raw/tvgRawLoader.cpp", "src/loaders/external_png/tvgPngLoader.cpp", "src/renderer/tvgAccessor.cpp", "src/renderer/tvgCanvas.cpp", "src/renderer/tvgFill.cpp", "src/renderer/tvgInitializer.cpp", "src/renderer/tvgLoader.cpp", "src/renderer/tvgPaint.cpp", "src/renderer/tvgPicture.cpp", "src/renderer/tvgRender.cpp", "src/renderer/tvgScene.cpp", "src/renderer/tvgShape.cpp", "src/renderer/tvgTaskScheduler.cpp", "src/renderer/tvgText.cpp", "src/renderer/sw_engine/tvgSwFill.cpp", "src/renderer/sw_engine/tvgSwImage.cpp", "src/renderer/sw_engine/tvgSwMath.cpp", "src/renderer/sw_engine/tvgSwMemPool.cpp", "src/renderer/sw_engine/tvgSwPostEffect.cpp", "src/renderer/sw_engine/tvgSwRaster.cpp", "src/renderer/sw_engine/tvgSwRenderer.cpp", "src/renderer/sw_engine/tvgSwRle.cpp", "src/renderer/sw_engine/tvgSwShape.cpp", "src/renderer/sw_engine/tvgSwStroke.cpp"}
    if R.truthy(R.index(env, "module_webp_enabled")) then
        thirdparty_sources = R.iadd(thirdparty_sources, {"src/loaders/external_webp/tvgWebpLoader.cpp"})
        env_svg:add({["CPPDEFINES"] = {"THORVG_WEBP_LOADER_SUPPORT"}})
    end
    if R.truthy(R.index(env, "module_jpg_enabled")) then
        thirdparty_sources = R.iadd(thirdparty_sources, {"src/loaders/external_jpg/tvgJpgLoader.cpp"})
        env_svg:add({["CPPDEFINES"] = {"THORVG_JPG_LOADER_SUPPORT"}})
    end
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_svg:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "inc")}})
    env_svg:add({["CPPDEFINES"] = {"TVG_STATIC"}})
    env_svg:add({["CPPDEFINES"] = {"THORVG_FILE_IO_SUPPORT"}})
    env_thirdparty = env_svg:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "src/common"), R.add(thirdparty_dir, "src/loaders/svg"), R.add(thirdparty_dir, "src/renderer"), R.add(thirdparty_dir, "src/renderer/sw_engine"), R.add(thirdparty_dir, "src/loaders/raw"), R.add(thirdparty_dir, "src/loaders/external_png")}})
    if R.truthy(R.index(env, "builtin_libpng")) then
        env_thirdparty:prepend({["CPPPATH"] = {"#thirdparty/libpng"}})
    end
    if R.truthy(R.index(env, "module_webp_enabled")) then
        env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "src/loaders/external_webp")}})
        if R.truthy(R.index(env, "builtin_libwebp")) then
            env_thirdparty:prepend({["CPPPATH"] = {"#thirdparty/libwebp/src"}})
        end
    end
    if R.truthy(R.index(env, "module_jpg_enabled")) then
        env_thirdparty:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "src/loaders/external_jpg")}})
        if R.truthy(R.index(env, "builtin_libjpeg_turbo")) then
            env_thirdparty:prepend({["CPPPATH"] = {"#thirdparty/libjpeg-turbo/src"}})
        end
    end
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_svg:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
