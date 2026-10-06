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
INTERNAL_SIGNALS = {("PhysicsServer2D", "_debug_changed"), ("PhysicsServer3D", "_debug_changed")}


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


def audit_enum_docs(row, path):
    """Compare documented enum identities and values with the actual ClassDB API."""
    expected = {
        value["name"]: (enum["name"], value["value"]) for enum in row.get("enums", []) for value in enum["values"]
    }
    if not expected:
        return [], 0
    name = row["name"]
    if path is None:
        return [f"Missing enum documentation: {name}"], 0
    documented = {node.get("name"): node for node in ET.parse(path).findall("constants/constant") if node.get("enum")}
    failures = []
    for member, (enum, value) in expected.items():
        node = documented.get(member)
        if node is None:
            failures.append(f"Undocumented enum constant: {name}.{member}")
        elif node.get("enum") != enum or int(node.get("value"), 0) != value:
            failures.append(f"Incorrect documented enum identity/value: {name}.{member}; expected {enum}={value}")
        elif not (node.text or "").strip():
            failures.append(f"Empty enum documentation: {name}.{member}")
    for member in documented.keys() - expected.keys():
        failures.append(f"Retired enum constant remains documented: {name}.{member}")
    return failures, len(expected)


def documented_type(node):
    """Use the extension API's type spelling, retaining enum and bitfield identity."""
    if node is None:
        return "void"
    if node.get("enum"):
        prefix = "bitfield::" if node.get("is_bitfield") == "true" else "enum::"
        return prefix + node.get("enum")
    kind = node.get("type", "void")
    if kind.endswith("[]"):
        return "typedarray::" + kind[:-2]
    return kind


def audit_method_docs(row, path, kind="method"):
    """Check public help signatures against a dump from the actual engine."""
    name = row["name"]
    methods = {
        method["name"]: method
        for method in row.get(kind + "s", [])
        if kind == "method" or (name, method["name"]) not in INTERNAL_SIGNALS
    }
    accessors = {prop.get(key) for prop in row.get("properties", []) for key in ("setter", "getter")}
    required = methods.keys() - (accessors if kind == "method" else set())
    if path is None:
        return ([f"Missing {kind} documentation: {name}"] if required else []), len(required)
    nodes = ET.parse(path).findall(kind + "s/" + kind)
    documented = {node.get("name"): node for node in nodes}
    failures = []
    if len(nodes) != len(documented):
        failures.append(f"Duplicate {kind} documentation: {name}")
    for member in sorted(required - documented.keys()):
        failures.append(f"Undocumented {kind}: {name}.{member}")
    for member, node in documented.items():
        method = methods.get(member)
        if method is None:
            failures.append(f"Retired {kind} remains documented: {name}.{member}")
            continue
        identity = f"{name}.{member}"
        if not node.findtext("description", "").strip():
            failures.append(f"Empty {kind} documentation: {identity}")
        expected_return = method.get("return_value", {}).get("type", "void")
        if documented_type(node.find("return")) != expected_return:
            failures.append(f"Incorrect documented return type: {identity}; expected {expected_return}")
        parameters = node.findall("param")
        arguments = method.get("arguments", [])
        if len(parameters) != len(arguments):
            failures.append(f"Incorrect documented argument count: {identity}; expected {len(arguments)}")
        for index, (param, argument) in enumerate(zip(parameters, arguments)):
            if (
                param.get("index") != str(index)
                or param.get("name") != argument["name"]
                or documented_type(param) != argument["type"]
                or param.get("default") != argument.get("default_value")
            ):
                failures.append(f"Incorrect documented argument: {identity}[{index}]; expected {argument}")
        qualifiers = set(node.get("qualifiers", "").split())
        for qualifier in ("const", "static", "vararg", "virtual"):
            if (qualifier in qualifiers) != method.get("is_" + qualifier, False):
                failures.append(f"Incorrect documented qualifier: {identity}; {qualifier}")
        if ("required" in qualifiers) != method.get("is_required", False):
            failures.append(f"Incorrect documented qualifier: {identity}; required")
    return failures, len(required)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--api", type=Path, required=True, help="Actual editor extension API dump")
    parser.add_argument("--sdk", type=Path, required=True, help="SDK extracted from the same editor")
    parser.add_argument("--managed", type=Path, required=True, help="modules/mono/glue/GodotSharp directory")
    parser.add_argument("--changes", type=Path, help="Required and removed API manifest")
    parser.add_argument("--classdb", type=Path, help="Actual property/signal snapshot from dump_egp_classdb.gd")
    parser.add_argument("--docs", type=Path, help="Repository root for XML method, signal, and enum checks")
    parser.add_argument("--output", type=Path, default=Path(".build/egp-api-validation/receipt.json"))
    args = parser.parse_args()
    receipt = {
        "passed": False,
        "checks": [],
        "native_only_hooks": [],
        "inspector_properties": [],
        "internal_signals": [],
        "enum_documentation": [],
        "method_documentation": [],
        "signal_documentation": [],
        "failures": [],
    }
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
        docs = {}
        if args.docs:
            for path in sorted(args.docs.glob("doc/classes/*.xml")) + sorted(
                args.docs.glob("modules/*/doc_classes/*.xml")
            ):
                if path.stem in docs:
                    failures.append(f"Duplicate class documentation: {path.stem}")
                docs[path.stem] = path
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
            if args.docs:
                path = docs.get(name)
                errors, count = audit_method_docs(row, path)
                failures.extend(errors)
                receipt["method_documentation"].append({
                    "class": name,
                    "methods": count,
                    "path": str(path) if path else None,
                    "sha256": digest(path) if path else None,
                })
            if args.docs and row.get("signals"):
                path = docs.get(name)
                errors, count = audit_method_docs(row, path, "signal")
                failures.extend(errors)
                receipt["signal_documentation"].append({
                    "class": name,
                    "signals": count,
                    "path": str(path) if path else None,
                    "sha256": digest(path) if path else None,
                })
            if args.docs and row.get("enums"):
                path = docs.get(name)
                errors, count = audit_enum_docs(row, path)
                failures.extend(errors)
                receipt["enum_documentation"].append({
                    "class": name,
                    "constants": count,
                    "path": str(path) if path else None,
                    "sha256": digest(path) if path else None,
                })
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
            # Match the generator's category/group filtering, retaining Variant
            # properties whose reflected type is NIL with NIL_IS_VARIANT usage.
            property_rows = [
                prop
                for prop in actual.get("properties", [])
                if not prop.get("usage", 0) & ((1 << 6) | (1 << 7) | (1 << 8))
                and not (prop.get("type") in (0, "Nil") and prop.get("usage", 0) & (1 << 18))
            ]
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
                if (name, signal) in INTERNAL_SIGNALS:
                    receipt["internal_signals"].append({"class": name, "signal": signal})
                    continue
                if signal not in cs_names:
                    failures.append(f"C# omits {name} signal {signal}")
            for prop in property_rows:
                if "/" in prop["name"]:
                    # The managed generator deliberately exposes these through Get/Set,
                    # rather than generating invalid C# identifiers for inspector paths.
                    receipt["inspector_properties"].append({
                        "class": name,
                        "property": prop["name"],
                        "type": prop["type"],
                    })
                elif prop["name"] not in cs_names:
                    failures.append(f"C# omits {name} property {prop['name']}")
            for member, expected_type in changes.get("property_types", {}).get(name, {}).items():
                prop = next((prop for prop in property_rows if prop["name"] == member), None)
                if prop is None or prop.get("type") != expected_type:
                    failures.append(
                        f"Property type check failed: {name}.{member}; expected Variant type {expected_type}"
                    )
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
