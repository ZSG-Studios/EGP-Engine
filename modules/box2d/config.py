def can_build(env, platform):
    return (
        not env["disable_physics_2d"]
        and env["precision"] == "single"
        and env["arch"] in ("x86_64", "arm64")
        and platform in ("windows", "linuxbsd", "macos")
    )


def configure(env):
    pass
