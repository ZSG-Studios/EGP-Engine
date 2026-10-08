-- Native documentation emitter for extension SDK builds.
function main(destination, directory, compressor)
    assert(compressor and os.isfile(compressor), "The native egp_compress host tool is required")
    local files = os.files(path.join(directory, "*.xml"))
    table.sort(files)
    local parts = {}
    for _, filename in ipairs(files) do
        parts[#parts + 1] = assert(io.readfile(filename)):gsub("\r\n", "\n")
    end
    local data = table.concat(parts)
    local input, output = os.tmpfile() .. ".xml", os.tmpfile() .. ".zlib"
    io.writefile(input, data, {encoding = "binary"})
    os.vrunv(compressor, {"--input", input, "--output", output, "--format", "zlib"})
    local compressed = assert(io.readfile(output, {encoding = "binary"}))
    os.tryrm(input)
    os.tryrm(output)
    local bytes = import("core.base.bytes")
    local lines = {"/* THIS FILE IS GENERATED DO NOT EDIT */", "", "#include <godot_cpp/godot.hpp>", "",
        'static const char *_doc_data_hash = "' .. hash.sha256(bytes(compressed)) .. '";',
        "static const int _doc_data_uncompressed_size = " .. #data .. ";",
        "static const int _doc_data_compressed_size = " .. #compressed .. ";",
        "static const unsigned char _doc_data_compressed[] = {"}
    for index = 1, #compressed do lines[#lines + 1] = "\t" .. compressed:byte(index) .. "," end
    lines[#lines + 1] = "};"
    lines[#lines + 1] = ""
    lines[#lines + 1] = "static ::godot::internal::DocDataRegistration _doc_data_registration(_doc_data_hash, _doc_data_uncompressed_size, _doc_data_compressed_size, _doc_data_compressed);"
    lines[#lines + 1] = ""
    io.writefile(destination, table.concat(lines, "\n") .. "\n", {encoding = "binary"})
end
