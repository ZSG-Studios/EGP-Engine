-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local basisu_encoder, encoder_sources, env, env_basisu, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, transcoder_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_basisu = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/basis_universal/"
    basisu_encoder = env.editor_build
    if R.truthy(basisu_encoder) then
        encoder_sources = {"3rdparty/android_astc_decomp.cpp", "basisu_astc_hdr_6x6_enc.cpp", "basisu_astc_hdr_common.cpp", "basisu_backend.cpp", "basisu_basis_file.cpp", "basisu_bc7enc.cpp", "basisu_comp.cpp", "basisu_enc.cpp", "basisu_etc.cpp", "basisu_frontend.cpp", "basisu_gpu_texture.cpp", "basisu_kernels_sse.cpp", "basisu_opencl.cpp", "basisu_pvrtc1_4.cpp", "basisu_resample_filters.cpp", "basisu_resampler.cpp", "basisu_ssim.cpp", "basisu_uastc_enc.cpp", "basisu_uastc_hdr_4x4_enc.cpp", "jpgd.cpp", "pvpngreader.cpp"}
        encoder_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(encoder_sources)) do; local file = __item2; table.insert(__item1, R.add(R.add(thirdparty_dir, "encoder/"), file)); end; return __item1 end)()
    end
    transcoder_sources = {R.add(thirdparty_dir, "transcoder/basisu_transcoder.cpp")}
    env_basisu:prepend({["CPPPATH"] = {thirdparty_dir}})
    if R.truthy(basisu_encoder) then
        env_basisu:prepend({["CPPPATH"] = {"#thirdparty/tinyexr"}})
    end
    if R.truthy(R.index(env, "builtin_zstd")) then
        env_basisu:prepend({["CPPPATH"] = {"#thirdparty/zstd"}})
    end
    env_thirdparty = env_basisu:clone()
    env_thirdparty:disable_warnings()
    if R.truthy(not R.truthy(env.msvc)) then
        env_thirdparty:add({["CCFLAGS"] = {"-fno-strict-aliasing"}})
    end
    env_thirdparty:add({["CPPDEFINES"] = {{"BASISD_SUPPORT_KTX2_ZSTD", 1}, {"BASISD_SUPPORT_ATC", 0}, {"BASISD_SUPPORT_FXT1", 0}, {"BASISD_SUPPORT_PVRTC1", 0}, {"BASISD_SUPPORT_PVRTC2", 0}}})
    if R.truthy(basisu_encoder) then
        env_thirdparty:sources(thirdparty_obj, encoder_sources)
    end
    env_thirdparty:sources(thirdparty_obj, transcoder_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_basisu:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
