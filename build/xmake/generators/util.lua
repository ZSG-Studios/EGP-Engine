local shared = import("support", {rootdir = os.scriptdir()})
function write(target, body, guard)
    return shared.write(target, body, guard)
end
function raw_cstring(value)
    if type(value) == 'table' then value = table.concat(value, '\n') .. '\n' end
    return shared.rawstring(value)
end
function format_buffer(value, indent, width)
    return shared.buffer(value)
end
function cstring(value) return shared.cstring(value) end
function compress(value, context) return shared.compress(value, context) end
