-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_thirdparty, env_webp, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_webp = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_libwebp")) then
        thirdparty_dir = "#thirdparty/libwebp/"
        thirdparty_sources = {"sharpyuv/sharpyuv.c", "sharpyuv/sharpyuv_cpu.c", "sharpyuv/sharpyuv_csp.c", "sharpyuv/sharpyuv_dsp.c", "sharpyuv/sharpyuv_gamma.c", "sharpyuv/sharpyuv_neon.c", "sharpyuv/sharpyuv_sse2.c", "src/dec/alpha_dec.c", "src/dec/buffer_dec.c", "src/dec/frame_dec.c", "src/dec/idec_dec.c", "src/dec/io_dec.c", "src/dec/quant_dec.c", "src/dec/tree_dec.c", "src/dec/vp8_dec.c", "src/dec/vp8l_dec.c", "src/dec/webp_dec.c", "src/demux/anim_decode.c", "src/demux/demux.c", "src/dsp/alpha_processing.c", "src/dsp/alpha_processing_mips_dsp_r2.c", "src/dsp/alpha_processing_neon.c", "src/dsp/alpha_processing_sse2.c", "src/dsp/alpha_processing_sse41.c", "src/dsp/cost.c", "src/dsp/cost_mips32.c", "src/dsp/cost_mips_dsp_r2.c", "src/dsp/cost_neon.c", "src/dsp/cost_sse2.c", "src/dsp/cpu.c", "src/dsp/dec.c", "src/dsp/dec_clip_tables.c", "src/dsp/dec_mips32.c", "src/dsp/dec_mips_dsp_r2.c", "src/dsp/dec_msa.c", "src/dsp/dec_neon.c", "src/dsp/dec_sse2.c", "src/dsp/dec_sse41.c", "src/dsp/enc.c", "src/dsp/enc_mips32.c", "src/dsp/enc_mips_dsp_r2.c", "src/dsp/enc_msa.c", "src/dsp/enc_neon.c", "src/dsp/enc_sse2.c", "src/dsp/enc_sse41.c", "src/dsp/filters.c", "src/dsp/filters_mips_dsp_r2.c", "src/dsp/filters_msa.c", "src/dsp/filters_neon.c", "src/dsp/filters_sse2.c", "src/dsp/lossless.c", "src/dsp/lossless_avx2.c", "src/dsp/lossless_enc.c", "src/dsp/lossless_enc_avx2.c", "src/dsp/lossless_enc_mips32.c", "src/dsp/lossless_enc_mips_dsp_r2.c", "src/dsp/lossless_enc_msa.c", "src/dsp/lossless_enc_neon.c", "src/dsp/lossless_enc_sse2.c", "src/dsp/lossless_enc_sse41.c", "src/dsp/lossless_mips_dsp_r2.c", "src/dsp/lossless_msa.c", "src/dsp/lossless_neon.c", "src/dsp/lossless_sse2.c", "src/dsp/lossless_sse41.c", "src/dsp/rescaler.c", "src/dsp/rescaler_mips32.c", "src/dsp/rescaler_mips_dsp_r2.c", "src/dsp/rescaler_msa.c", "src/dsp/rescaler_neon.c", "src/dsp/rescaler_sse2.c", "src/dsp/ssim.c", "src/dsp/ssim_sse2.c", "src/dsp/upsampling.c", "src/dsp/upsampling_mips_dsp_r2.c", "src/dsp/upsampling_msa.c", "src/dsp/upsampling_neon.c", "src/dsp/upsampling_sse2.c", "src/dsp/upsampling_sse41.c", "src/dsp/yuv.c", "src/dsp/yuv_mips32.c", "src/dsp/yuv_mips_dsp_r2.c", "src/dsp/yuv_neon.c", "src/dsp/yuv_sse2.c", "src/dsp/yuv_sse41.c", "src/enc/alpha_enc.c", "src/enc/analysis_enc.c", "src/enc/backward_references_cost_enc.c", "src/enc/backward_references_enc.c", "src/enc/config_enc.c", "src/enc/cost_enc.c", "src/enc/filter_enc.c", "src/enc/frame_enc.c", "src/enc/histogram_enc.c", "src/enc/iterator_enc.c", "src/enc/near_lossless_enc.c", "src/enc/picture_csp_enc.c", "src/enc/picture_enc.c", "src/enc/picture_psnr_enc.c", "src/enc/picture_rescale_enc.c", "src/enc/picture_tools_enc.c", "src/enc/predictor_enc.c", "src/enc/quant_enc.c", "src/enc/syntax_enc.c", "src/enc/token_enc.c", "src/enc/tree_enc.c", "src/enc/vp8l_enc.c", "src/enc/webp_enc.c", "src/mux/anim_encode.c", "src/mux/muxedit.c", "src/mux/muxinternal.c", "src/mux/muxread.c", "src/utils/bit_reader_utils.c", "src/utils/bit_writer_utils.c", "src/utils/color_cache_utils.c", "src/utils/filters_utils.c", "src/utils/huffman_encode_utils.c", "src/utils/huffman_utils.c", "src/utils/palette.c", "src/utils/quant_levels_dec_utils.c", "src/utils/quant_levels_utils.c", "src/utils/random_utils.c", "src/utils/rescaler_utils.c", "src/utils/thread_utils.c", "src/utils/utils.c"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_webp:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "src/")}})
        env_thirdparty = env_webp:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_webp:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
