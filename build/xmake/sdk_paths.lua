-- Shared native dependency routes, resolved before source recipes and reused at link/package time.
local function enabled(value) return value == true or value == 'y' or value == 'yes' or value == 'true' or value == '1' end

function dependencies(root)
    local override = os.getenv('EGP_BUILD_DEPS')
    if override and override ~= '' then return path.absolute(override, root) end
    local localdata = os.getenv('LOCALAPPDATA')
    if localdata and not os.getenv('MSYSTEM') then return path.join(localdata, 'Godot/build_deps') end
    return path.join(root, 'bin/build_deps')
end

function compiler(options)
    if not enabled(options.use_mingw) then return 'msvc' end
    return enabled(options.use_llvm) and 'llvm' or 'gcc'
end

local function route(options, name, fallback, root)
    local value = options[name]
    return path.absolute(value and value ~= '' and value or fallback, root)
end

function variant(base, arch, compiler_name)
    local candidate = base .. '-' .. arch .. '-' .. compiler_name
    return os.isdir(candidate) and candidate or base
end

function validate_mesa(directory)
    local source = path.join(directory, 'godot-mesa')
    local version = path.join(source, 'VERSION.info')
    if not os.isfile(version) then version = path.join(source, 'VERSION') end
    assert(os.isfile(version), 'Direct3D 12 Mesa SDK version file missing: ' .. version .. '; run xmake lua misc/scripts/install_build_dependencies.lua d3d12')
    local value = io.readfile(version):trim()
    local major, minor = value:match('^(%d+)%.(%d+)%.%d+[%w%.%+%-]*$')
    assert(major and tonumber(major) == 25 and tonumber(minor) >= 3, 'Direct3D 12 requires Mesa 25.3 or newer within major 25; invalid/outdated SDK version: ' .. value .. ' (' .. version .. ')')
    return value
end

function resolve(options, root)
    root = root or os.projectdir()
    if options.platform ~= 'windows' and options.godot_platform ~= 'windows' then return options end
    local deps = dependencies(root)
    local arch = ({x64='x86_64', x86='x86_32', aarch64='arm64'})[options.arch] or options.arch or 'x86_64'
    if enabled(options.d3d12) then
        options.mesa_libs = variant(route(options, 'mesa_libs', path.join(deps, 'mesa'), root), arch, compiler(options))
        validate_mesa(options.mesa_libs)
        options.agility_sdk_path = route(options, 'agility_sdk_path', path.join(deps, 'agility_sdk'), root)
        options.pix_path = route(options, 'pix_path', path.join(deps, 'pix'), root)
    end
    if enabled(options.angle) then
        options.angle_libs = variant(route(options, 'angle_libs', path.join(deps, 'angle'), root), arch, compiler(options))
    end
    return options
end
