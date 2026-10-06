"""Check physics/networking ClassDB exposure against the generated C# and C++ APIs."""

import argparse
import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

LEGACY_CLASSES = {
    "MultiplayerAPI",
    "MultiplayerAPIExtension",
    "MultiplayerPeer",
    "MultiplayerPeerExtension",
    "SceneMultiplayer",
    "MultiplayerSpawner",
    "MultiplayerSynchronizer",
    "ENetConnection",
    "ENetMultiplayerPeer",
    "ENetPacketPeer",
    "WebSocketMultiplayerPeer",
    "WebRTCMultiplayerPeer",
    "WebRTCPeerConnection",
    "WebRTCPeerConnectionExtension",
    "WebRTCDataChannel",
    "WebRTCDataChannelExtension",
    "EGPLiteSession",
}
REQUIRED_CLASSES = {"PhysicsServer2D", "PhysicsServer3D", "EGPBox3DWorld", "EGPNetSession"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def snake(name):
    name = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return name.replace("2_D", "2D").replace("3_D", "3D").lower()


def managed_classes(root):
    result = {}
    for props in root.glob("*/Generated/GeneratedIncludes.props"):
        project = props.parent.parent
        for node in ET.parse(props).iter("Compile"):
            path = project / node.attrib["Include"].replace("\\", "/")
            text = path.read_text(encoding="utf-8")
            match = re.search(r'\[GodotClassName\("([^"]+)"\)\]', text)
            if match:
                result[match.group(1)] = (path, text)
            elif "GodotObjects" in path.parts:
                match = re.search(r"public\s+(?:\w+\s+)*class\s+(\w+)", text)
                if match:
                    result[match.group(1)] = (path, text)
    if not result:
        raise RuntimeError("No compiled generated C# classes found")
    return result


def in_scope(name):
    return name.startswith(("Physics", "Box2D", "Box3D", "EGP")) or name in {
        "Area2D",
        "Area3D",
        "RigidBody2D",
        "RigidBody3D",
        "StaticBody2D",
        "StaticBody3D",
        "CharacterBody2D",
        "CharacterBody3D",
        "AnimatableBody2D",
        "AnimatableBody3D",
        "Joint2D",
        "Joint3D",
        "PinJoint2D",
        "PinJoint3D",
        "HingeJoint3D",
        "SliderJoint3D",
        "ConeTwistJoint3D",
        "Generic6DOFJoint3D",
        "DampedSpringJoint2D",
        "GrooveJoint2D",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--api", type=Path, required=True, help="Actual editor extension API dump")
    parser.add_argument("--sdk", type=Path, required=True, help="SDK extracted from the same editor")
    parser.add_argument("--managed", type=Path, required=True, help="modules/mono/glue/GodotSharp directory")
    parser.add_argument("--changes", type=Path, help="Required and removed API manifest")
    parser.add_argument("--classdb", type=Path, help="Actual property/signal snapshot from dump_egp_classdb.gd")
    parser.add_argument("--output", type=Path, default=Path(".build/egp-api-validation/receipt.json"))
    args = parser.parse_args()
    receipt = {"passed": False, "checks": [], "native_only_hooks": [], "inspector_properties": [], "failures": []}
    failures = receipt["failures"]
    try:
        api = json.loads(args.api.read_text(encoding="utf-8"))
        classes = {row["name"]: row for row in api["classes"]}
        csharp = managed_classes(args.managed)
        metadata = json.loads((args.sdk / "sdk.json").read_text(encoding="utf-8"))
        receipt["extension_api_sha256"] = digest(args.api)
        receipt["sdk_metadata"] = metadata
        if metadata.get("api_sha256") != receipt["extension_api_sha256"]:
            failures.append("C++ SDK does not record the same actual API hash")
        if metadata.get("precision") != api["header"]["precision"]:
            failures.append("C++ SDK precision differs from the actual editor API")
        changes = json.loads(args.changes.read_text(encoding="utf-8")) if args.changes else {}
        reflected = {}
        if args.classdb:
            reflection = json.loads(args.classdb.read_text(encoding="utf-8"))
            if reflection.get("extension_api_sha256") != receipt["extension_api_sha256"]:
                failures.append("ClassDB snapshot does not record the same actual API hash")
            engine = Path(reflection.get("engine", ""))
            if not engine.is_file() or digest(engine) != reflection.get("engine_sha256"):
                failures.append("ClassDB snapshot engine is missing or has changed")
            receipt["classdb_sha256"] = digest(args.classdb)
            receipt["classdb_engine_sha256"] = reflection.get("engine_sha256")
            reflected = reflection.get("classes", {})
        elif any(
            members.get("properties")
            for direction in ("required", "removed")
            for members in changes.get(direction, {}).values()
        ) or changes.get("property_types"):
            failures.append("Property acceptance requires --classdb from the actual editor")
        native_only = changes.get("native_only_hooks", {})
        for name, members in native_only.items():
            methods_by_name = {method["name"]: method for method in classes.get(name, {}).get("methods", [])}
            for member in members:
                method = methods_by_name.get(member, {})
                types = [arg["type"] for arg in method.get("arguments", [])]
                types.append(method.get("return_value", {}).get("type", "void"))
                if (
                    not name.endswith("Extension")
                    or not method.get("is_virtual")
                    or not any("*" in kind for kind in types)
                ):
                    failures.append(f"Invalid native-only pointer hook declaration: {name}.{member}")
                else:
                    receipt["native_only_hooks"].append({"class": name, "method": member, "types": types})
        removed = LEGACY_CLASSES | set(changes.get("removed_classes", []))
        required = REQUIRED_CLASSES | set(changes.get("required_classes", [])) | set(changes.get("required", {}))
        for name in sorted(required):
            if name not in classes:
                failures.append(f"Missing required ClassDB class: {name}")
        for name in sorted(removed):
            if name in classes or name in csharp:
                failures.append(f"Retired class remains in native/managed API: {name}")
            if (args.sdk / "gen/include/godot_cpp/classes" / (snake(name) + ".hpp")).exists():
                failures.append(f"Retired class remains in C++ SDK: {name}")
        for name, row in sorted(classes.items()):
            if (
                not in_scope(name)
                and name not in changes.get("required", {})
                and name not in changes.get("removed", {})
            ):
                continue
            entry = {"class": name, "methods": len(row.get("methods", []))}
            receipt["checks"].append(entry)
            if name not in csharp:
                failures.append(f"Missing generated C# class: {name}")
                continue
            cs_path, cs_text = csharp[name]
            cpp_path = args.sdk / "gen/include/godot_cpp/classes" / (snake(name) + ".hpp")
            if not cpp_path.is_file():
                failures.append(f"Missing C++ header: {name}")
                continue
            cpp_text = cpp_path.read_text(encoding="utf-8")
            cs_names = set(re.findall(r'StringName\s+\w+\s*=\s*"([^"]+)"', cs_text))
            entry["csharp_sha256"] = digest(cs_path)
            entry["cpp_sha256"] = digest(cpp_path)
            methods = {method["name"] for method in row.get("methods", [])}
            constants = {value["name"] for enum in row.get("enums", []) for value in enum["values"]}
            constants.update(constant["name"] for constant in row.get("constants", []))
            actual = reflected.get(name, row)
            if args.classdb and name not in reflected:
                failures.append(f"ClassDB snapshot omits audited class: {name}")
            signals = {signal["name"] for signal in actual.get("signals", [])}
            property_rows = [prop for prop in actual.get("properties", []) if prop.get("type") not in (0, "Nil")]
            properties = {prop["name"] for prop in property_rows}
            for method in sorted(methods):
                if method not in cs_names and method not in native_only.get(name, []):
                    failures.append(f"C# omits {name}.{method}")
                if not re.search(r"\b" + re.escape(method) + r"\s*\(", cpp_text):
                    failures.append(f"C++ omits {name}.{method}")
            for constant in sorted(constants):
                if not re.search(r"\b" + re.escape(constant) + r"\b", cpp_text):
                    failures.append(f"C++ omits {name}.{constant}")
            for signal in sorted(signals):
                if signal not in cs_names:
                    failures.append(f"C# omits {name} signal {signal}")
            for prop in property_rows:
                if "/" in prop["name"]:
                    # The managed generator deliberately exposes these through Get/Set,
                    # rather than generating invalid C# identifiers for inspector paths.
                    receipt["inspector_properties"].append({"class": name, "property": prop["name"], "type": prop["type"]})
                elif prop["name"] not in cs_names:
                    failures.append(f"C# omits {name} property {prop['name']}")
            for member, expected_type in changes.get("property_types", {}).get(name, {}).items():
                prop = next((prop for prop in property_rows if prop["name"] == member), None)
                if prop is None or prop.get("type") != expected_type:
                    failures.append(f"Property type check failed: {name}.{member}; expected Variant type {expected_type}")
            for direction in ("required", "removed"):
                delta = changes.get(direction, {}).get(name, {})
                for kind, names in (
                    ("methods", methods),
                    ("constants", constants),
                    ("signals", signals),
                    ("properties", properties),
                ):
                    for member in delta.get(kind, []):
                        if (member in names) != (direction == "required"):
                            failures.append(f"{direction} {kind} check failed: {name}.{member}")
                        if direction == "removed" and kind == "methods":
                            if member in cs_names or re.search(r"\b" + re.escape(member) + r"\s*\(", cpp_text):
                                failures.append(f"Retired method remains in generated API: {name}.{member}")
                        if direction == "removed" and kind == "constants":
                            if re.search(r"\b" + re.escape(member) + r"\b", cpp_text):
                                failures.append(f"Retired constant remains in C++ API: {name}.{member}")
        receipt["passed"] = not failures
    except (OSError, ValueError, RuntimeError, ET.ParseError) as error:
        failures.append(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    for failure in failures:
        print("FAIL: " + failure, file=sys.stderr)
    print(f"{'PASS' if receipt['passed'] else 'FAIL'}: {len(receipt['checks'])} class binding checks; {args.output}")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
