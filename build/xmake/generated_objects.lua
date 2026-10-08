-- Keep generated translation-unit objects bounded without moving ordinary source caches.
local bytes = import("core.base.bytes")
local config = import("core.project.config")

function apply(target, generated)
    local root = path.absolute(generated, os.projectdir())
    local replacements, identities, source_replacements = {}, {}, {}
    for _, batch in pairs(target:sourcebatches()) do
        for index, source in ipairs(batch.sourcefiles or {}) do
            local absolute = path.absolute(source, os.projectdir())
            if os.host() == "windows" and not path.is_absolute(source) and #os.projectdir() + 1 + #source >= 240 then
                target:fileconfig_set(absolute, table.clone(target:fileconfig(source) or {}))
                source_replacements[source] = absolute
            end
            local relative = path.relative(path.absolute(source, os.projectdir()), root):gsub("\\", "/")
            local is_generated = relative ~= ".." and not relative:startswith("../") and not path.is_absolute(relative)
            local original = batch.objectfiles and batch.objectfiles[index]
            local dependency = batch.dependfiles and batch.dependfiles[index]
            local too_long = os.host() == "windows" and original and
                (#path.absolute(original, os.projectdir()) >= 240 or
                (not path.is_absolute(original) and #os.projectdir() + 1 + #original >= 240) or
                (dependency and (#path.absolute(dependency, os.projectdir()) >= 240 or
                (not path.is_absolute(dependency) and #os.projectdir() + 1 + #dependency >= 240))))
            if original and (is_generated or too_long) then
                local identity = is_generated and "generated\n" .. relative or
                    "ordinary\n" .. path.relative(path.absolute(source, os.projectdir()), os.projectdir()):gsub("\\", "/")
                local extension = path.extension(original)
                local namespace = table.concat({target:fullname(), target:plat(), target:arch(), config.get("mode") or ""}, "\n")
                local object = path.join(path.absolute(config.builddir(), os.projectdir()), "generated-objects", hash.sha256(bytes(namespace .. "\n" .. identity)) .. extension)
                assert(not identities[object] or identities[object] == identity, "Compact object identity collision")
                assert(os.host() ~= "windows" or (#path.absolute(object) < 240 and #path.absolute(object .. ".d") < 240), "Compact object/dependency directory exceeds the safe Windows path bound")
                identities[object], replacements[original] = identity, object
            end
        end
    end
    -- Native source discovery makes external files relative again. Keep the
    -- public aggregate and compiler batches aligned, including per-file policy.
    for index, source in ipairs(target:sourcefiles()) do
        target:sourcefiles()[index] = source_replacements[source] or source
    end
    -- Update the public aggregate too if another native rule already requested it.
    local objects = target:objectfiles()
    for index, object in ipairs(objects) do objects[index] = replacements[object] or object end
    for _, batch in pairs(target:sourcebatches()) do
        for index, source in ipairs(batch.sourcefiles or {}) do
            batch.sourcefiles[index] = source_replacements[source] or source
        end
        for index, original in ipairs(batch.objectfiles or {}) do
            local object = replacements[original]
            if object then
                batch.objectfiles[index] = object
                batch.dependfiles[index] = object .. ".d"
            end
        end
    end
    target:data_set("egp.generated_objects.mapping", replacements)
    target:data_set("egp.generated_objects.sources", source_replacements)
end

function configure(target, generated)
    local previous = target:script("config")
    target:set("config", function(current, options)
        if previous then previous(current, options) end
        apply(current, generated)
    end)
end
