-- Prepare the complete upstream test project from the same source snapshot as the SDK.
function source_files(project)
    local files = {}
    for _, filename in ipairs(os.files(path.join(project, '**'))) do
        local relative = path.relative(filename, project):gsub('\\', '/')
        if not relative:startswith('project/bin/') then
            table.insert(files, filename)
        end
    end
    return files
end

function validate_templates(project, include)
    local count = 0
    for _, filename in ipairs(os.files(path.join(project, 'src/**'))) do
        for header in assert(io.readfile(filename)):gmatch('#include%s*<(godot_cpp/templates/[^>]+)>') do
            assert(os.isfile(path.join(include, header)), 'Pinned C++ fixture requires a template absent from its SDK source: ' .. header)
            count = count + 1
        end
    end
    assert(count > 0, 'Pinned C++ fixture must cover native template headers')
    return count
end

function main(destination, root)
    root = root or os.projectdir()
    local source = path.join(root, 'thirdparty/godot-cpp')
    destination = path.absolute(destination or '.build/upstream-cpp-fixture', root)
    local relative = path.relative(destination, root):gsub('\\', '/')
    assert(relative:startswith('.build/'), 'Copy the pinned C++ fixture into an isolated engine .build directory: ' .. relative)
    local revision = assert(io.readfile(path.join(source, 'UPSTREAM.md')):match('`(%x+)`'))
    assert(#revision == 40, 'Vendored C++ fixture provenance requires a full upstream revision')
    local project = path.join(destination, 'test')
    local files = 0
    for _, filename in ipairs(source_files(path.join(source, 'test'))) do
        local output = path.join(project, path.relative(filename, path.join(source, 'test')))
        os.mkdir(path.directory(output)); os.cp(filename, output)
        files = files + 1
    end
    local patch = path.join(root, '.github/actions/test-godot-cpp/egp-test.patch')
    os.vrunv('git', {'apply', '--check', '--unidiff-zero', '--directory=' .. relative, patch}, {curdir=root})
    os.vrunv('git', {'apply', '--unidiff-zero', '--directory=' .. relative, patch}, {curdir=root})
    local templates = validate_templates(project, path.join(source, 'include'))
    local receipt = {revision=revision, source=source, project=project, files=files, template_includes=templates, patch_sha256=hash.sha256(patch)}
    io.writefile(path.join(destination, 'provenance.json'), import('core.base.json').encode(receipt))
    print('PINNED_GODOT_CPP_FIXTURE_PASS ' .. revision .. ' files=' .. files .. ' templates=' .. templates)
    return receipt
end
