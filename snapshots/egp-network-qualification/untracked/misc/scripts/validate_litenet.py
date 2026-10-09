#!/usr/bin/env python3
"""Reproducible EGP networking qualification; exits nonzero on a failed check."""
import argparse
import json
import hashlib
import pathlib
import shutil
import subprocess
import tempfile
from xml.sax.saxutils import escape

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(args, log, cwd=ROOT, timeout=180):
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(args, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Failed ({result.returncode}): {' '.join(map(str, args))}; see {log}")


def validate_package(receipts, native_library=None):
    # Each run restores the just-built package into a fresh cache, even when its
    # development version hasn't changed. A project reference would hide bad packing.
    with tempfile.TemporaryDirectory(prefix="package-", dir=receipts) as temporary:
        fixture = pathlib.Path(temporary)
        cache = fixture / "cache"
        feed = ROOT / "bin/GodotSharp/Tools/nupkgs"
        properties = f"""<PropertyGroup>
<TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings>
<Nullable>enable</Nullable><RestorePackagesPath>{escape(str(cache))}</RestorePackagesPath>
<RestoreAdditionalProjectSources>{escape(str(feed))}</RestoreAdditionalProjectSources>
<EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup>
<ItemGroup><PackageReference Include="EGP.Networking" Version="[0.1.0]" /></ItemGroup>"""
        consumer = fixture / "Consumer.csproj"
        sources = "".join(f'<Compile Include="{escape(str(ROOT / "modules/litenet/tests" / name))}" />'
                          for name in ("Program.cs", "NativePhysicsWorld.cs", "RpcDecoderChecks.cs"))
        consumer.write_text(f'<Project Sdk="Microsoft.NET.Sdk">{properties}'
                            f'<PropertyGroup><OutputType>Exe</OutputType></PropertyGroup>'
                            f'<ItemGroup>{sources}</ItemGroup></Project>', encoding="utf-8")
        run(["dotnet", "build", consumer, "-c", "Release"], receipts / "package-build.log")
        args = ["dotnet", fixture / "bin/Release/net10.0/Consumer.dll"]
        if native_library:
            shutil.copy2(native_library, fixture / "bin/Release/net10.0" / native_library.name)
            args.append("--native-physics")
        run(args, receipts / "package-runtime.log", timeout=90)
        invalid = fixture / "Invalid.cs"
        invalid.write_text("""using LiteEntitySystem;
class InvalidEntity : EntityLogic {
    public SyncVar<int> Health;
    public InvalidEntity(EntityParams p) : base(p) { }
    public void InvalidReset() { Health = new SyncVar<int>(); }
}
""", encoding="utf-8")
        analyzer = fixture / "Analyzer.csproj"
        analyzer.write_text(f'<Project Sdk="Microsoft.NET.Sdk">{properties}'
                            '<ItemGroup><Compile Include="Invalid.cs" /></ItemGroup></Project>', encoding="utf-8")
        log = receipts / "package-analyzer.log"
        with log.open("w", encoding="utf-8") as output:
            result = subprocess.run(["dotnet", "build", analyzer, "-c", "Release"],
                                    cwd=fixture, stdout=output, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode == 0 or "error LES0001" not in log.read_text(encoding="utf-8"):
            raise RuntimeError(f"Packaged SyncVar analyzer did not reject invalid assignment; see {log}")
    (receipts / "package-result.json").write_text(
        json.dumps({"passed": True, "clean_consumer": True, "analyzer_error": "LES0001"}, indent=2) + "\n",
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-physics", action="store_true")
    parser.add_argument("--package-checks", action="store_true")
    parser.add_argument("--engine", type=pathlib.Path)
    options = parser.parse_args()
    receipts = ROOT / ".build/litenet-validation"
    receipts.mkdir(parents=True, exist_ok=True)
    project = ROOT / "modules/litenet/tests/EGP.Networking.Tests.csproj"
    for library in ("litenetlib", "liteentitysystem"):
        vendor = ROOT / "thirdparty" / library
        manifest = json.loads((vendor / "source_manifest.json").read_text(encoding="utf-8"))
        if manifest.get("text_hash_normalization") != "cs-il-crlf-to-lf":
            raise RuntimeError(f"Unknown source manifest normalization: {library}")
        for relative, metadata in manifest["files"].items():
            data = (vendor / relative).read_bytes()
            if pathlib.Path(relative).suffix in (".cs", ".il"):
                data = data.replace(b"\r\n", b"\n")
            if hashlib.sha256(data).hexdigest() != metadata["sha256"]:
                raise RuntimeError(f"Pinned source changed without a reviewed manifest update: {library}/{relative}")
    run(["dotnet", "restore", ROOT / "modules/litenet/managed/EGP.Networking.csproj", "--locked-mode"], receipts / "restore.log")
    run(["dotnet", "build", project, "-c", "Release", "--no-incremental"], receipts / "build.log")
    args = ["dotnet", "run", "--project", project, "-c", "Release", "--no-build", "--"]
    library = None
    if options.native_physics:
        native = ROOT / ".build/litenet-native"
        run(["cmake", "-S", ROOT / "modules/litenet/tests/native", "-B", native], receipts / "native-configure.log")
        run(["cmake", "--build", native, "--config", "Release", "-j", "4"], receipts / "native-build.log", timeout=600)
        output = project.parent / "bin/Release/net10.0"
        library = native / "Release/egp_network_physics.dll"
        if not library.exists():
            library = next(native.rglob("*egp_network_physics.so"), None) or next(native.rglob("*egp_network_physics.dylib"), None)
        if library is None or not library.exists():
            raise RuntimeError("Native networking test library missing")
        shutil.copy2(library, output / library.name)
        args.append("--native-physics")
    run(args, receipts / "managed-and-native.log", timeout=90)
    report_text = (receipts / "managed-and-native.log").read_text(encoding="utf-8")
    report, _ = json.JSONDecoder().raw_decode(report_text[report_text.index("{"):])
    if not report["passed"]:
        raise RuntimeError("Networking test report failed")
    (receipts / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if options.package_checks:
        validate_package(receipts, library)
    if options.engine:
        sample = ROOT / "modules/litenet/samples/godot"
        # Build sample against the engine's matching packaged Godot.NET.Sdk first.
        run([str(options.engine.resolve()), "--headless", "--max-fps", "30", "--path", str(sample),
             "--", "--report=" + str(receipts / "godot-result.json")], receipts / "godot-runtime.log", timeout=45)
        godot_report = json.loads((receipts / "godot-result.json").read_text(encoding="utf-8"))
        if not godot_report["passed"]:
            raise RuntimeError("Godot networking integration failed")
    print(f"Passed {len(report['checks'])} networking checks; receipts: {receipts}")


if __name__ == "__main__":
    main()
