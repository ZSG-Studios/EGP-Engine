def can_build(env, platform):
    # Initial qualified simulation profile. Other targets need their own determinism evidence.
    return (
        not env["disable_physics_3d"]
        and env["precision"] == "single"
        and env["arch"] in ("x86_64", "arm64")
        and platform in ("windows", "linuxbsd", "macos")
    )


def configure(env):
    pass


def get_doc_classes():
    return ["EGPBox3DWorld"]


def get_doc_path():
    return "doc_classes"
