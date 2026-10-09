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


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [BoolVariable("box3d_scene_backend", "Build the experimental Box3D PhysicsServer3D scene adapter", False)]


def get_doc_classes():
    return ["EGPBox3DWorld"]


def get_doc_path():
    return "doc_classes"
