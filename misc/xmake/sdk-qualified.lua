-- Isolated consumer of the shared engine graph's native Lua SDK generator.
-- Invocation: xmake lua misc/xmake/sdk-qualified.lua ROOT OVERLAY API COMPRESS
function main(root, overlay, api, compress)
    root, overlay, api, compress = path.absolute(root), path.absolute(overlay), path.absolute(api), path.absolute(compress)
    os.cd(root)
    assert(os.isfile(api) and os.isfile(compress), "Actual API and native compression tool required")
    local json = import("core.base.json")
    local bytes = import("core.base.bytes")
    local sdk = import("build.xmake.generators.native_extension_sdk", {rootdir = root})
    local output = path.join(overlay, "editor/settings/gdextension/native_extension_sdk.gen.h")
    local result = sdk.main({cpp_root = path.join(root, "thirdparty/godot-cpp"),
        template_root = path.join(root, "editor/settings/gdextension/cpp_sdk"),
        api_file = api, bits = "64", precision = "single",
        generation_directory = path.join(path.directory(overlay), "qualified-sdk-bindings"),
        extract_directory = path.join(path.directory(overlay), "qualified-sdk"),
        target = output, compress_executable = compress})
    result.api_sha256 = hash.sha256(bytes(assert(io.readfile(api, {encoding = "binary"}))))
    result.header_sha256 = hash.sha256(bytes(assert(io.readfile(output, {encoding = "binary"}))))
    result.compress_sha256 = hash.sha256(bytes(assert(io.readfile(compress, {encoding = "binary"}))))
    io.writefile(path.join(path.directory(overlay), "sdk-receipt.json"), json.encode(result, {indent = true}))
    print("Qualified SDK regenerated from actual native engine API: " .. result.api_sha256)
end
