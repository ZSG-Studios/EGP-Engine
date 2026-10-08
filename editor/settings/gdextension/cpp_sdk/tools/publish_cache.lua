-- Atomic, first-writer-wins publication. Each process owns its staging file.
function main(source, destination)
    assert(os.isfile(source), "Compiled SDK archive is missing")
    try {function () os.mkdir(path.directory(destination)) end, catch {function (errors)
        if not os.isdir(path.directory(destination)) then raise(errors) end
    end}}
    assert(os.isdir(path.directory(destination)), "Compiled SDK cache directory is unavailable")
    local temporary = destination .. "." .. hash.uuid() .. ".tmp"
    os.cp(source, temporary)
    if os.isfile(destination) then
        os.rm(temporary)
        return
    end
    local ok = os.trymv(temporary, destination)
    if not ok then
        os.rm(temporary)
        assert(os.isfile(destination), "Could not atomically publish compiled SDK cache")
    end
end
