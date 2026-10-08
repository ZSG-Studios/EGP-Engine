local templates = {
    ["197"] = "#include \"servers/rendering/renderer_rd/shader_rd.h\"\n\nclass @class_name@ : public ShaderRD {\npublic:\n\t@class_name@() {\n",
    ["241"] = "\t}\n};\n",
    ["279"] = "static const char @os.path.basename(shader).replace('.glsl', '_shader_glsl')@[] = {\n@to_raw_cstring(header_data.code)@\n};\n",
    ["217"] = "\t\tsetup_raytracing(_raygen_code, _any_hit_code, _closest_hit_code, _miss_code, _intersection_code, \"@class_name@\");\n",
    ["221"] = "\t\tstatic const char *_vertex_code = nullptr;\n\t\tstatic const char *_fragment_code = nullptr;\n\t\tstatic const char _compute_code[] = {\n@compute_code@\n\t\t};\n\t\tsetup(_vertex_code, _fragment_code, _compute_code, \"@class_name@\");\n",
    ["230"] = "\t\tstatic const char _vertex_code[] = {\n@vertex_code@\n\t\t};\n\t\tstatic const char _fragment_code[] = {\n@fragment_code@\n\t\t};\n\t\tstatic const char *_compute_code = nullptr;\n\t\tsetup(_vertex_code, _fragment_code, _compute_code, \"@class_name@\");\n",
}
function get(name) return assert(templates[name], "Unknown shader template: " .. name) end
