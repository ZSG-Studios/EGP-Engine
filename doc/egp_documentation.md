# EGP documentation and website

EGP maintains two forks of Godot's official publishing projects:

- [EGP-docs](https://github.com/ZSG-Studios/EGP-docs): the manual and class reference, built with Sphinx and Godot's existing documentation theme.
- [EGP-website](https://github.com/ZSG-Studios/EGP-website): the project website, using Godot's Jekyll styles and layouts.

The documentation describes EGP's Box2D/Box3D physics, native Superpos,
matching generated GDScript/C#/C++ bindings, C++ extension editor, runtime reload, and
xmake workflow. Inherited multiplayer and Jolt instructions are replaced
with migration guidance. Upstream credit and licenses remain intact.

## Update the manual and class reference

Edit native class descriptions in `doc/classes/*.xml` or the corresponding
module's `doc_classes` directory. Edit system guides in this engine's `doc/`
directory and the networking guide in `doc/egp_superpos.md`. The website
fork owns its introductory pages and navigation; the documentation fork owns
its additional tutorials and migration guides.

Clone the documentation fork beside the engine, then run from the engine root:

```powershell
git clone https://github.com/ZSG-Studios/EGP-docs.git ../EGP-docs
python misc/scripts/sync_egp_docs.py --docs ../EGP-docs
python misc/scripts/sync_egp_docs.py --docs ../EGP-docs --check
```

Synchronization regenerates the entire native class reference with Godot's
official XML-to-reStructuredText tool, removes retired class pages, publishes
the canonical guides and native Superpos language contracts, and records source
and output SHA256 hashes in `egp/source_manifest.json`. Review the generated
diff before committing. Commit engine source first when publishing a snapshot,
so the manifest identifies a fetchable source revision.

## Build and verify the documentation

From the documentation checkout:

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements.txt
.venv/Scripts/python.exe -m sphinx -b html -W --keep-going . _build/html
python -m http.server 8070 --directory _build/html
```

On Linux or macOS use `.venv/bin/python`. Open `http://localhost:8070` to
review navigation, search, language tabs, generated signatures, and both color
themes. The CI workflow builds the same HTML and retains it as an artifact.
Public hosting uses a separate, manually triggered deployment workflow.

Documentation builds verify markup and references. Validate native API
descriptions against the actual engine with `validate_egp_api.py --docs` and
the matching API/ClassDB/compiled-help captures described in
[the API contract](egp_api_contract.md). Runtime behavior and platform support
still require the corresponding engine fixtures.

## Upstream maintenance

Both forks retain an `upstream` remote pointing to their Godot source repository.
Merge upstream changes explicitly, review changes to replaced systems and
branding, then synchronize against the intended EGP engine revision and rebuild.
Do not run Godot's original class-reference synchronization workflow: it would
restore APIs that EGP removed. EGP's replacement workflow reads the pinned
engine revision from its source manifest.

The manual retains Godot's CC BY 3.0 attribution. Generated class documentation
retains the engine's MIT license. The project website retains its original
license and upstream attribution. These forks are maintained by ZSG-Studios;
they are not official Godot project services.
