-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local astc_encoder, env, env_astcenc, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_astcenc = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/astcenc/"
    thirdparty_sources = {"astcenc_averages_and_directions.cpp", "astcenc_block_sizes.cpp", "astcenc_color_quantize.cpp", "astcenc_color_unquantize.cpp", "astcenc_compress_symbolic.cpp", "astcenc_compute_variance.cpp", "astcenc_decompress_symbolic.cpp", "astcenc_diagnostic_trace.cpp", "astcenc_entry.cpp", "astcenc_find_best_partitioning.cpp", "astcenc_ideal_endpoints_and_weights.cpp", "astcenc_image.cpp", "astcenc_integer_sequence.cpp", "astcenc_mathlib.cpp", "astcenc_mathlib_softfloat.cpp", "astcenc_partition_tables.cpp", "astcenc_percentile_tables.cpp", "astcenc_pick_best_endpoint_format.cpp", "astcenc_quantization.cpp", "astcenc_symbolic_physical.cpp", "astcenc_weight_align.cpp", "astcenc_weight_quant_xfer_tables.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_astcenc:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_thirdparty = env_astcenc:clone()
    env_thirdparty:disable_warnings()
    astc_encoder = env.editor_build
    if R.truthy(not R.truthy(astc_encoder)) then
        env_thirdparty:add({["CPPDEFINES"] = {"ASTCENC_DECOMPRESS_ONLY"}})
    end
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_astcenc:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
