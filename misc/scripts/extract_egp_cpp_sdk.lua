-- Generate and extract a matching SDK using the native Lua emitter.
function main(...)
    local argv, options, index = {...}, {}, 1
    while index <= #argv do
        local name, value = argv[index]:match("^%-%-([^=]+)=(.*)$")
        if not name then name = assert(argv[index]:match("^%-%-(.+)$")); index = index + 1; value = assert(argv[index]) end
        options[name] = value; index = index + 1
    end
    local root = path.absolute(options.root or os.projectdir())
    local output = path.absolute(assert(options.output, "--output is required"))
    local generator = import("build.xmake.generators.native_extension_sdk", {rootdir = root})
    local receipt = generator.main({cpp_root = path.join(root, "thirdparty/godot-cpp"),
        template_root = path.join(root, "editor/settings/gdextension/cpp_sdk"),
        api_file = path.absolute(assert(options.api, "--api actual engine dump is required")),
        generation_directory = output .. "-bindings", extract_directory = output,
        bits = options.bits or "64", precision = options.precision or "single"})
    print(receipt.archive_sha256, receipt.file_count, output)
end
