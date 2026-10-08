-- Managed language compilation is orchestrated by xmake; no Python build driver.

local function write_if_changed(filename, content)
    if os.isfile(filename) and io.readfile(filename) == content then return end
    os.mkdir(path.directory(filename)); io.writefile(filename, content)
end

local function versions(root)
    local info = import("version", {rootdir = root}).info()
    info.status = os.getenv("GODOT_VERSION_STATUS") or info.status
    local base = info.major .. "." .. info.minor .. "." .. info.patch
    local sharp, net = base, base
    if info.status == "stable" then net = ".*"
    else
        local label, number = info.status:match("^(.-)(%d+)$")
        if number then
            sharp = sharp .. "-" .. label .. "." .. number
            net = net .. "-" .. (label == "dev" and "alpha" or label) .. "." .. number
        else sharp, net = sharp .. "-" .. info.status, net .. "-dev" end
    end
    local defines = {"GODOT" .. info.major, "GODOT" .. info.major .. "_" .. info.minor, "GODOT" .. info.major .. "_" .. info.minor .. "_" .. info.patch}
    for major = 4, info.major do table.insert(defines, "GODOT" .. major .. "_OR_GREATER") end
    for minor = 0, info.minor do table.insert(defines, "GODOT" .. info.major .. "_" .. minor .. "_OR_GREATER") end
    for patch = 0, info.patch do table.insert(defines, "GODOT" .. info.major .. "_" .. info.minor .. "_" .. patch .. "_OR_GREATER") end
    write_if_changed(path.join(root, "modules/mono/SdkPackageVersions.props"), string.format([[<Project>
  <PropertyGroup>
    <PackageVersion_GodotSharp>%s</PackageVersion_GodotSharp>
    <PackageVersion_Godot_NET_Sdk>%s</PackageVersion_Godot_NET_Sdk>
    <PackageVersion_Godot_SourceGenerators>%s</PackageVersion_Godot_SourceGenerators>
    <PackageVersion_GodotDotNet>%s</PackageVersion_GodotDotNet>
    <_GodotVersionConstants>%s</_GodotVersionConstants>
  </PropertyGroup>
</Project>
]], sharp, sharp, sharp, net, table.concat(defines, ";")))
    write_if_changed(path.join(root, "modules/mono/editor/Godot.NET.Sdk/Godot.SourceGenerators/Generated/Common.Constants.cs"), [[namespace Godot.SourceGenerators
{
// TODO: This is currently disabled because of https://github.com/dotnet/roslyn/issues/52904
#pragma warning disable IDE0040 // Add accessibility modifiers.
    partial class Common
    {
        public const string VersionDocsUrl = "https://docs.godotengine.org/en/]] .. info.docs_branch .. [[";
    }
}
]])
end

function parse_options(arguments)
    local options = {}
    local flags = {["no-deprecated"] = true, werror = true, ["dev-debug"] = true}
    for _, argument in ipairs(arguments) do
        local key, value = argument:match("^([%w_-]+)=(.*)$")
        key = key or argument
        assert(flags[key] or key == "push-nupkgs-local", "Unknown managed build option: " .. key)
        if flags[key] then
            options[key] = value == nil or import("common", {rootdir = os.scriptdir()}).enabled(value)
        else
            assert(value and value ~= "", "push-nupkgs-local requires a directory")
            options[key] = path.absolute(value)
        end
    end
    return options
end

function build_arguments(solution, configuration, precision, options, flags)
    local argv = {"msbuild", solution, "/restore", "/t:Build", "/p:Configuration=" .. configuration}
    if precision == "double" then table.insert(argv, "/p:GodotFloat64=true") end
    if options["no-deprecated"] then table.insert(argv, "/p:GodotNoDeprecated=true") end
    if options.werror then table.insert(argv, "/p:TreatWarningsAsErrors=true") end
    if options["push-nupkgs-local"] then
        table.insert(argv, "/p:ClearNuGetLocalCache=true")
        table.insert(argv, "/p:PushNuGetToLocalSource=" .. options["push-nupkgs-local"])
    end
    for _, flag in ipairs(flags or {}) do table.insert(argv, flag) end
    return argv
end

-- These assemblies are platform-neutral; device AOT/RID export is a separate SDK concern.
function validate_platform(platform)
    assert(import("common", {rootdir = os.scriptdir()}).supports_mono(platform), "Unsupported managed platform: " .. tostring(platform))
    return platform
end

function main(editor, platform, precision, ...)
    local root = path.absolute(path.join(os.scriptdir(), "../.."))
    assert(editor and os.isfile(path.absolute(editor)), "Pass the matching built editor executable")
    platform, precision = platform or "windows", precision or "single"
    validate_platform(platform)
    assert(precision == "single" or precision == "double", "Invalid managed precision")
    local options = parse_options({...})
    versions(root)
    local dotnet = assert(import("lib.detect.find_program")("dotnet"), "Install the .NET SDK required by modules/mono/global.json")
    local module_dir = path.join(root, "modules/mono")
    local function build(solution, configuration, flags)
        local argv = build_arguments(path.join(module_dir, solution), configuration, precision, options, flags)
        os.vrunv(dotnet, argv, {curdir = module_dir, envs = {PLATFORM = ""}, timeout = 600000})
    end
    local filenames = {"GodotSharp.dll", "GodotSharp.pdb", "GodotSharp.xml", "GodotSharpEditor.dll", "GodotSharpEditor.pdb", "GodotSharpEditor.xml", "GodotPlugins.dll", "GodotPlugins.pdb", "GodotPlugins.runtimeconfig.json"}
    for _, configuration in ipairs({"Debug", "Release"}) do
        build("glue/GodotSharp/GodotSharp.sln", configuration, {"/p:NoWarn=1591"})
        local destination = path.join(root, "bin/GodotSharp/Api", configuration)
        os.mkdir(destination)
        for _, filename in ipairs(filenames) do
            local located
            for _, subproject in ipairs({"GodotSharp", "GodotSharpEditor", "GodotPlugins"}) do
                local source = path.join(module_dir, "glue/GodotSharp", subproject, "bin", configuration, subproject == "GodotPlugins" and "net10.0" or "", filename)
                if os.isfile(source) then located = source; break end
            end
            assert(located, "Managed build did not publish " .. filename)
            os.cp(located, path.join(destination, filename))
        end
    end
    build("editor/GodotTools/GodotTools.sln", options["dev-debug"] and "Debug" or "Release", {"/p:GodotPlatform=" .. platform})
    build("editor/Godot.NET.Sdk/Godot.NET.Sdk.sln", "Release")
    print("EGP_MANAGED_XMAKE_PASS")
end
