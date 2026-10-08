"""Export the immersive fixture and link an unsigned visionOS device application."""

import argparse
import hashlib
import json
import plistlib
import shutil
import subprocess
from pathlib import Path


def run(command, log, timeout):
    with log.open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=timeout, check=True)
    text = log.read_text(encoding="utf-8", errors="replace")
    if "ERROR:" in text or "SCRIPT ERROR:" in text:
        raise RuntimeError(f"Errors in {log}")
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    engine, template, output = args.engine.resolve(), args.template.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sample = output / "sample"
    shutil.copytree(root / "misc/egp/visionos_forward_plus", sample, ignore=shutil.ignore_patterns(".godot", "build"))
    # The exporter requires a nonempty team even for project-only exports. This
    # placeholder is not an Apple account; signing is disabled for the link test.
    with (sample / "export_presets.cfg").open("a", encoding="utf-8") as preset:
        preset.write('\napplication/app_store_team_id="0000000000"\n')
        preset.write(f"custom_template/debug={json.dumps(str(template))}\n")
    common = [str(engine), "--headless", "--path", str(sample)]
    run([*common, "--editor", "--import"], output / "import.log", 300)
    project_dir = output / "xcode-project"
    project_dir.mkdir()
    run(
        [*common, "--export-debug", "visionOS Experimental", str(project_dir / "EGPProbe.zip")],
        output / "export.log",
        300,
    )
    project = project_dir / "EGPProbe.xcodeproj"
    if not (project / "project.pbxproj").is_file():
        raise RuntimeError("Exporter did not produce the Xcode project")
    derived = output / "derived-data"
    build_log = run(
        [
            "xcodebuild",
            "-project",
            str(project),
            "-scheme",
            "EGPProbe",
            "-configuration",
            "Debug",
            "-sdk",
            "xros",
            "-destination",
            "generic/platform=visionOS",
            "-derivedDataPath",
            str(derived),
            "CODE_SIGNING_ALLOWED=NO",
            "CODE_SIGNING_REQUIRED=NO",
            "CODE_SIGN_IDENTITY=",
            "build",
        ],
        output / "xcodebuild.log",
        900,
    )
    if "** BUILD SUCCEEDED **" not in build_log:
        raise RuntimeError("Xcode did not report a successful device build")
    app = derived / "Build/Products/Debug-xros/EGPProbe.app"
    with (app / "Info.plist").open("rb") as source:
        info = plistlib.load(source)
    manifest = info["UIApplicationSceneManifest"]
    if manifest["UIApplicationPreferredDefaultSceneSessionRole"] != "CPSceneSessionRoleImmersiveSpaceApplication":
        raise RuntimeError("Exported app does not start in an immersive space")
    scenes = manifest["UISceneConfigurations"]["UISceneSessionRoleImmersiveSpaceApplication"]
    if scenes[0]["UISceneInitialImmersionStyle"] != "UIImmersionStyleFull":
        raise RuntimeError("Exported app does not use the requested Full immersion mode")
    executable = app / info["CFBundleExecutable"]
    if executable.stat().st_size == 0 or not list(app.rglob("*.pck")):
        raise RuntimeError("Device app is missing its executable or exported project pack")
    architectures = run(["xcrun", "lipo", "-archs", str(executable)], output / "architectures.txt", 30).strip()
    if architectures != "arm64":
        raise RuntimeError(f"Unexpected device architectures: {architectures}")
    # Ship a project ready for the owner's signing configuration, not the CI placeholder.
    pbxproj = project / "project.pbxproj"
    pbxproj.write_text(pbxproj.read_text().replace("DEVELOPMENT_TEAM = 0000000000;", 'DEVELOPMENT_TEAM = "";'))
    shutil.copytree(app, output / "unsigned-app/EGPProbe.app")
    receipt = {
        "status": "PASS",
        "scope": "Sample imported, exported and linked for a physical visionOS device; unsigned and not executed",
        "device_validated": False,
        "signing_validated": False,
        "architectures": architectures,
        "bundle_identifier": info["CFBundleIdentifier"],
        "immersion_style": "Full",
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "template_sha256": hashlib.sha256(template.read_bytes()).hexdigest(),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    }
    (output / "export-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("EGP_VISIONOS_EXPORT_PASS", json.dumps(receipt))


if __name__ == "__main__":
    main()
