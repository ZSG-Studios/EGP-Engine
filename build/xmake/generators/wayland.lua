-- Native Wayland binding generation. No shell pipeline or interpreter is used.
function generate(job, context)
    local modes = { ["wayland.scanner.client_header"] = "client-header", ["wayland.scanner.private_code"] = "private-code" }
    local mode = modes[job.builder]
    if not mode then return false end
    assert(#job.targets == 1 and #job.sources >= 2, "Malformed Wayland scanner job")
    local util = import("support", {rootdir = os.scriptdir()})
    local scanner = context.wayland_scanner or os.getenv("WAYLAND_SCANNER") or import("lib.detect.find_program")("wayland-scanner")
    assert(scanner, "wayland-scanner is required when Wayland is enabled; install the native Wayland development tools")
    local source, target = util.sourcepath(job.sources[1], context), job.targets[1]
    assert(os.isfile(source), "Wayland protocol XML missing: " .. source)
    os.mkdir(path.directory(target))
    -- A temporary output avoids publishing partial headers if the scanner fails.
    local temporary = os.tmpfile() .. ".wayland"
    os.vrunv(scanner, {"-c", mode, source, temporary}, {timeout = 60000})
    local value = assert(io.readfile(temporary), "Wayland scanner did not produce an output")
    if job.sources[2].value then
        value = value:gsub("wayland%-client%-core%.h", "../dynwrappers/wayland-client-core-so_wrap.h")
        value = value:gsub("wayland%-util%.h", "../dynwrappers/wayland-client-core-so_wrap.h")
    end
    if not os.isfile(target) or io.readfile(target) ~= value then io.writefile(target, value) end
    os.tryrm(temporary)
    return true
end
