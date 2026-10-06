# EGP

EGP is a fork of [Godot Engine](https://github.com/godotengine/godot),
maintained by ZSG-Studios and based on upstream `master`.

## Features

- Box2D and Box3D physics.
- Yojimbo networking with GDScript, C# and C++ APIs.
- Built-in C++ extension tools and a bundled godot-cpp SDK.
- FASTBuild support for local and distributed Windows builds.

## Building from source

On Windows, install Visual Studio's C++ tools, the Windows SDK and the .NET SDK,
then run from the repository root:

```powershell
.\misc\scripts\build_egp.ps1 -Setup
.\misc\scripts\build_egp.ps1 -Local -Target editor
```

See the [FASTBuild guide](doc/egp_fastbuild.md) for export templates and
distributed builds. For other platforms, see Godot's
[compilation instructions](https://docs.godotengine.org/en/latest/engine_details/development/compiling).

## Documentation

- [EGP documentation source and website](doc/egp_documentation.md)
- [Documentation fork](https://github.com/ZSG-Studios/EGP-docs) · [Website fork](https://github.com/ZSG-Studios/EGP-website)
- [C++ extensions](doc/egp_cpp_extensions.md)
- [Networking](modules/egp_net/README.md)
- [Network lab](doc/egp_network_lab.md)
- [Box2D physics](doc/egp_box2d.md)
- [Box3D physics](doc/egp_box3d.md)
- [API and runtime reload contracts](doc/egp_api_contract.md)
- [FASTBuild](doc/egp_fastbuild.md)
- [Godot documentation](https://docs.godotengine.org)

## Contributing

Report bugs and suggest changes through [GitHub issues](https://github.com/ZSG-Studios/EGP/issues).
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

EGP uses Godot's [MIT license](LICENSE.txt). See [AUTHORS.md](AUTHORS.md)
and [COPYRIGHT.txt](COPYRIGHT.txt) for credits and third-party licenses.
