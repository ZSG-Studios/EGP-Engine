"""Build-time packaging for EGP's offline, pre-generated godot-cpp SDK."""

import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path


def make_archive(cpp_root, template_root, bits="64", precision="single", api_file=None):
    cpp_root, template_root = Path(cpp_root), Path(template_root)
    api_file = Path(api_file) if api_file else cpp_root / "gdextension/extension_api-4-7.json"
    if not (cpp_root / "binding_generator.py").is_file():
        raise RuntimeError("Bundled godot-cpp is missing. Run git submodule update --init --depth 1.")
    spec = importlib.util.spec_from_file_location("egp_binding_generator", cpp_root / "binding_generator.py")
    generator = importlib.util.module_from_spec(spec)
    # Godot's build imports a different module with this same name.
    previous_interface = sys.modules.pop("make_interface_header", None)
    sys.path.insert(0, str(cpp_root.resolve()))
    try:
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory() as temp:
            generator.generate_bindings(str(api_file), str(cpp_root / "gdextension/gdextension_interface.json"),
                                        True, bits=bits, precision=precision, output_dir=temp)
            files = {}
            for folder in ("include", "src"):
                for path in sorted((cpp_root / folder).rglob("*")):
                    if path.is_file():
                        files[path.relative_to(cpp_root).as_posix()] = path.read_bytes()
            for path in sorted((Path(temp) / "gen").rglob("*")):
                if path.is_file():
                    files[path.relative_to(temp).as_posix()] = path.read_bytes()
            files["LICENSE.md"] = (cpp_root / "LICENSE.md").read_bytes()
            for path in sorted(template_root.rglob("*")):
                if path.is_file():
                    files[path.relative_to(template_root).as_posix()] = path.read_bytes()
            api = json.loads(api_file.read_text(encoding="utf-8"))["header"]
            files["templates/extension.gdextension.in"] = files["templates/extension.gdextension.in"].replace(
                b"@API@", f'{api["version_major"]}.{api["version_minor"]}'.encode())
            files["sdk.json"] = json.dumps({"api_major": api["version_major"], "api_minor": api["version_minor"],
                                           "bits": bits, "precision": precision}, sort_keys=True).encode()
            files["CMakeLists.txt"] = files["CMakeLists.txt"].replace(b"@BITS@", bits.encode()).replace(
                b"@PRECISION_DEFINE@", b"REAL_T_IS_DOUBLE" if precision == "double" else b"")
            archive = bytearray()
            for name, data in sorted(files.items()):
                name = name.encode("utf-8")
                archive += struct.pack("<II", len(name), len(data)) + name + data
            return bytes(archive)
    finally:
        sys.path.pop(0)
        sys.modules.pop("make_interface_header", None)
        if previous_interface is not None:
            sys.modules["make_interface_header"] = previous_interface


def build_header(target, source, env):
    root = Path(str(source[0])).resolve().parent
    templates = Path(str(source[1])).resolve().parent
    archive = make_archive(root, templates, env["egp_cpp_bits"], env["precision"], env.get("egp_cpp_api"))
    compressed = zlib.compress(archive, 9)
    digest = hashlib.sha256(archive).hexdigest()
    output = Path(str(target[0]))
    with output.open("w", encoding="utf-8", newline="\n") as header:
        header.write('// Generated EGP godot-cpp SDK. Do not edit.\n#pragma once\n')
        header.write(f'inline constexpr const char *egp_cpp_sdk_hash = "{digest}";\n')
        header.write(f'inline constexpr int egp_cpp_sdk_size = {len(archive)};\n')
        header.write('inline constexpr unsigned char egp_cpp_sdk_data[] = {\n')
        for offset in range(0, len(compressed), 32):
            header.write(",".join(str(value) for value in compressed[offset:offset + 32]) + ",\n")
        header.write('};\n')


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--api")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    archive = make_archive(repo / "thirdparty/godot-cpp", Path(__file__).with_name("cpp_sdk"), api_file=args.api)
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    offset = 0
    while offset < len(archive):
        name_length, data_length = struct.unpack_from("<II", archive, offset)
        offset += 8
        name = archive[offset:offset + name_length].decode()
        offset += name_length
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(archive[offset:offset + data_length])
        offset += data_length
    print(f"Extracted SDK to {output}")
